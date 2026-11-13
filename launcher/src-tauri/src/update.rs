// SPDX-License-Identifier: GPL-3.0-or-later
//! The in-launcher update of the release releases.rs picked.
//!
//! `install`: download, verify, extract, swap, all through `<install>/.update/`:
//! 1. SHA256SUMS.txt first, so a release without a checksum for the package costs no 170 MB. The package is streamed to `.update/`
//!    and hashed as it arrives. Nothing outside `.update/` has changed until step 3, so cancel and every failure here only delete it.
//! 2. The system tar unpacks it to `.update/staged/`, and the tree is checked: launcher and tools/build in `PokopiaRecomp-<v>/`.
//! 3. `apply` (update_apply.rs): each file of the package takes the place of its old self, which goes to `.update/backup/`; every
//!    step can be undone, and a start after a power cut restores the backup.
//! 4. `relaunch`: the new launcher starts with `--updated` and this one exits. The running exe cannot be deleted on Windows but can
//!    be renamed, so it becomes `<name>.old` and the next start deletes it (`clean_start`).
//!
//! Renames only, inside one folder tree, so a swap cannot run out of disk space halfway. A second launcher window leaves a running
//! update alone: `.update/lock` is held for the whole update.
//! The pure parts take plain paths and no AppHandle: the tests below build a fake install and run them.

use crate::download;
use crate::update_apply::{apply, exists, recover, rollback_message, with_old, Applied, Job, MARKER};
use crate::releases::{get, user_agent, Target};
use std::fs::{self, File, OpenOptions, TryLockError};
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering::SeqCst};
use std::time::Duration;

/// The package is ~170 MB: connect and silence limits, never a total one.
const CONNECT_TIMEOUT: Duration = Duration::from_secs(20);
const READ_TIMEOUT: Duration = Duration::from_secs(30);

pub const UPDATE_DIR: &str = ".update";
const LOCK: &str = "lock";
/// install_update's Err when the player cancelled, for the page to tell it from a failure.
pub const CANCELLED: &str = "Update cancelled.";

#[cfg(windows)]
const PACKAGE_LAUNCHER: &str = "PokopiaLauncher.exe";
#[cfg(target_os = "macos")]
// The binary at the root of the release zip's PokopiaRecomp-<version>/ folder: that folder is the install root (exe_dir), so
// the package replaces the launcher and tools/ in place, as on Windows and Linux.
const PACKAGE_LAUNCHER: &str = "PokopiaLauncher";
#[cfg(not(any(windows, target_os = "macos")))]
const PACKAGE_LAUNCHER: &str = "PokopiaLauncher.AppImage";


/// The SHA-256 SHA256SUMS.txt lists for `name` (`<hex> *<name>` from the release job, `<hex>  <name>` from sha256sum), lower case.
pub fn sum_for(sums: &str, name: &str) -> Option<String> {
    sums.trim_start_matches('\u{feff}').lines().find_map(|l| {
        let (hex, rest) = l.trim().split_once(char::is_whitespace)?;
        let file = rest.trim_start().trim_start_matches('*');
        (file == name && hex.len() == 64 && hex.bytes().all(|b| b.is_ascii_hexdigit())).then(|| hex.to_ascii_lowercase())
    })
}

/// Room for the download and its unpacked copy. Unknown space passes: extraction would report a full drive anyway.
pub fn enough_space(free: Option<u64>, asset_size: u64) -> bool {
    free.is_none_or(|f| f >= asset_size.saturating_mul(2))
}

/// `update-progress` events for one phase: the first, the last, and one per percent in between.
#[derive(Default)]
struct Throttle {
    phase: String,
    last: u64,
}

impl Throttle {
    fn pass(&mut self, phase: &str, done: u64, total: u64) -> bool {
        if phase != self.phase || done == total || done.saturating_sub(self.last) >= (total / 100).max(1) {
            self.phase = phase.to_string();
            self.last = done;
            return true;
        }
        false
    }
}

/// The system's tar: bsdtar in System32 on Windows, which reads the zip too (never one found on PATH).
fn tar() -> Command {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let root = std::env::var_os("SystemRoot").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(r"C:\Windows"));
        let mut cmd = Command::new(root.join("System32").join("tar.exe"));
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
        cmd
    }
    #[cfg(not(windows))]
    {
        crate::host::command("tar")
    }
}

/// Unpacks `dir/archive` into a new `dir/out`. Names relative to `dir`: Windows' tar cannot open a path with characters outside the
/// system code page, and an install in such a folder must update too. Its error output goes to a file so a full pipe can never stall it.
pub fn extract(dir: &Path, archive: &str, out: &str, cancel: &AtomicBool) -> Result<(), String> {
    fs::create_dir_all(dir.join(out)).map_err(|e| format!("Could not unpack the update: {}. Nothing was changed.", e))?;
    let log = dir.join("tar.log");
    let err = File::create(&log).map_err(|e| format!("Could not unpack the update: {}. Nothing was changed.", e))?;
    let mut child = tar()
        .current_dir(dir)
        .arg("-xf")
        .arg(archive)
        .arg("-C")
        .arg(out)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(err)
        .spawn()
        .map_err(|e| format!("Could not start the system's tar to unpack the update: {}. Nothing was changed.", e))?;
    let status = loop {
        if cancel.load(SeqCst) {
            let _ = child.kill();
            let _ = child.wait();
            return Err(CANCELLED.into());
        }
        match child.try_wait().map_err(|e| e.to_string())? {
            Some(s) => break s,
            None => std::thread::sleep(Duration::from_millis(100)),
        }
    };
    if status.success() {
        return Ok(());
    }
    let text = fs::read_to_string(&log).unwrap_or_default();
    let last = text.lines().last().unwrap_or("").trim().to_string();
    Err(format!("Could not unpack the update ({}). Check that the drive has free space, then try again. Nothing was changed.", if last.is_empty() { status.to_string() } else { last }))
}

/// `staged/PokopiaRecomp-<version>`, once it has the launcher and tools/build. Asked before the install is touched.
pub fn find_package(staged: &Path, version: &str, launcher: &str) -> Result<PathBuf, String> {
    // Every host's zip puts the same tree under PokopiaRecomp-<version>/: the launcher, tools/ and the runtime library. That
    // folder is the install root, and an update replaces what is in it in place.
    let root = staged.join(format!("PokopiaRecomp-{}", version));
    if root.join(launcher).is_file() && root.join("tools").join("build").is_dir() {
        return Ok(root);
    }
    Err("The downloaded package is not laid out as expected. Nothing was changed.".into())
}


/// Another launcher is in the middle of an update here.
fn locked(work: &Path) -> bool {
    OpenOptions::new().write(true).open(work.join(LOCK)).is_ok_and(|f| matches!(f.try_lock(), Err(TryLockError::WouldBlock)))
}

/// Start of a GUI run: finishes off the last update. Waits for the launcher that restarted us, which holds `.update/lock` and its own
/// exe until it has exited. Runs on a thread of its own: deleting the old tools/ takes seconds, and `install` waits for it.
/// `old` is the exact list of `.old` files an update makes: a plain `*.old` here would also delete a player's `pkp.ini.old`.
pub fn clean_start(root: &Path, old: &[PathBuf]) {
    let work = root.join(UPDATE_DIR);
    let mut tries = 0;
    while locked(&work) {
        tries += 1;
        if tries > 40 {
            return; // a second window, with an update of its own running
        }
        std::thread::sleep(Duration::from_millis(250));
    }
    let stuck = work.join(MARKER).is_file() && recover(root, &work) > 0;
    if work.is_dir() && !stuck {
        let _ = fs::remove_dir_all(&work);
    }
    for _ in 0..40 {
        let left: Vec<&PathBuf> = old.iter().filter(|p| exists(p)).collect();
        if left.is_empty() {
            break;
        }
        for p in left {
            let _ = fs::remove_file(p);
        }
        std::thread::sleep(Duration::from_millis(250));
    }
}

/// The `.old` files of `launcher` and of a custom game folder.
pub fn old_files(launcher: &Path, game_dir: &Path) -> Vec<PathBuf> {
    vec![with_old(launcher), with_old(&crate::host::runtime_lib(game_dir))]
}

/// The update's flags. `busy` also keeps Play and Set up from starting under it.
#[derive(Default)]
pub struct UpdateState {
    busy: AtomicBool,
    cancel: AtomicBool,
    /// files are moving: the window must not close
    applying: AtomicBool,
    /// `clean_start` runs
    cleaning: AtomicBool,
}

impl UpdateState {
    pub fn busy(&self) -> bool {
        self.busy.load(SeqCst)
    }
    pub fn applying(&self) -> bool {
        self.applying.load(SeqCst)
    }
    pub fn cancel(&self) {
        self.cancel.store(true, SeqCst);
    }
    pub fn set_cleaning(&self, on: bool) {
        self.cleaning.store(on, SeqCst);
    }
    /// None: an update already runs.
    pub fn claim(&self) -> Option<Claim<'_>> {
        if self.busy.swap(true, SeqCst) {
            return None;
        }
        self.cancel.store(false, SeqCst);
        Some(Claim(self))
    }
}

/// Clears `busy` and `applying` when the update ends, however it ends.
pub struct Claim<'a>(&'a UpdateState);

impl Drop for Claim<'_> {
    fn drop(&mut self) {
        self.0.applying.store(false, SeqCst);
        self.0.busy.store(false, SeqCst);
    }
}

pub struct Paths {
    pub install: PathBuf,
    /// the running launcher
    pub launcher: PathBuf,
    pub game_dir: PathBuf,
}

/// The game's pkp_rt.dll when the game folder is not the install.
fn game_dll(p: &Paths) -> Option<PathBuf> {
    let canon = |d: &Path| fs::canonicalize(d).unwrap_or_else(|_| d.to_path_buf());
    let dll = crate::host::runtime_lib(&p.game_dir);
    (canon(&p.game_dir) != canon(&p.install) && dll.is_file()).then_some(dll)
}

/// A swap that is done, with the lock still held.
pub struct Session {
    applied: Applied,
    work: PathBuf,
    lock: Option<File>,
}

impl Session {
    /// The new launcher runs: the backup can go.
    pub fn finish(mut self) {
        drop(self.lock.take());
        let _ = fs::remove_dir_all(&self.work);
    }

    /// The new launcher did not start: back to the old install. Err: what could not be put back.
    pub fn rollback(mut self) -> Result<(), String> {
        let failed = std::mem::take(&mut self.applied).undo();
        drop(self.lock.take());
        if failed.is_empty() {
            let _ = fs::remove_dir_all(&self.work);
            return Ok(());
        }
        let _ = fs::write(self.work.join(MARKER), "");
        Err(rollback_message(&failed))
    }
}

fn reset_work(root: &Path, work: &Path) -> Result<(), String> {
    if locked(work) {
        return Err("Another launcher window is updating. Close it first.".into());
    }
    if work.join(MARKER).is_file() && recover(root, work) > 0 {
        return Err("An earlier update did not finish and could not be undone. Extract the release again.".into());
    }
    if exists(work) {
        fs::remove_dir_all(work).map_err(|e| format!("Could not clear {}: {}.", work.display(), e))?;
    }
    fs::create_dir_all(work).map_err(|e| format!("The launcher cannot write in {}: {}. Nothing was changed.", root.display(), e))
}

/// Steps 1 to 3 of the header. `ready` says whether the install may still change (the game or setup may have started meanwhile), right
/// before the swap. Err: nothing changed, unless it says otherwise. `report` gets (phase, done, total), about 100 per phase.
pub fn install(t: &Target, p: &Paths, st: &UpdateState, report: &mut dyn FnMut(&str, u64, u64), ready: &dyn Fn() -> Result<(), String>) -> Result<Session, String> {
    while st.cleaning.load(SeqCst) {
        if st.cancel.load(SeqCst) {
            return Err(CANCELLED.into());
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    let work = p.install.join(UPDATE_DIR);
    reset_work(&p.install, &work)?;
    let lock = File::create(work.join(LOCK)).ok().filter(|f| f.try_lock().is_ok()).ok_or("Another launcher window is updating. Close it first.")?;
    match stages(t, p, st, &work, report, ready) {
        Ok(applied) => Ok(Session { applied, work, lock: Some(lock) }),
        Err(e) => {
            drop(lock);
            // a failed rollback keeps the marker and the backup for the next start
            if !work.join(MARKER).is_file() {
                let _ = fs::remove_dir_all(&work);
            }
            Err(e)
        }
    }
}

fn stages(t: &Target, p: &Paths, st: &UpdateState, work: &Path, report: &mut dyn FnMut(&str, u64, u64), ready: &dyn Fn() -> Result<(), String>) -> Result<Applied, String> {
    let free = crate::preflight::free_bytes(&p.install);
    if !enough_space(free, t.size) {
        let gb = |b: u64| format!("{:.1} GB", b as f64 / crate::preflight::GB as f64);
        return Err(format!("Not enough free disk space on {}: the update needs about {} and {} is free.", crate::preflight::drive_of(&p.install), gb(t.size.saturating_mul(2)), gb(free.unwrap_or(0))));
    }
    let mut gate = Throttle::default();
    let mut send = |phase: &str, done: u64, total: u64| {
        if gate.pass(phase, done, total) {
            report(phase, done, total);
        }
    };
    send("download", 0, t.size);
    let agent = ureq::AgentBuilder::new().timeout_connect(CONNECT_TIMEOUT).timeout_read(READ_TIMEOUT).user_agent(&user_agent(env!("CARGO_PKG_VERSION"))).build();
    let failed = |e: String| format!("The download failed: {}. Nothing was changed. Try again.", e);
    let sums = String::from_utf8_lossy(&get(&agent, &t.sums_url, 1 << 16).map_err(failed)?).into_owned();
    let want = sum_for(&sums, &t.asset).ok_or("This release has no checksum for its download yet. Try again in a while. Nothing was changed.")?;

    let archive = work.join(&t.asset);
    let resp = agent.get(&t.asset_url).call().map_err(|e| failed(e.to_string()))?;
    let save_failed = |e: io::Error| format!("Could not save the update: {}. Nothing was changed.", e);
    let mut out = File::create(&archive).map_err(save_failed)?;
    let say = download::Said { read: &|e| format!("The download failed: {}. Nothing was changed. Try again.", e), write: &|e| format!("Could not save the update: {}. Nothing was changed.", e) };
    let (n, got) = download::copy_checked(&mut resp.into_reader(), &mut out, t.size, &st.cancel, &mut |n| send("download", n, t.size), &say)?.ok_or(CANCELLED)?;
    out.sync_all().map_err(save_failed)?;
    drop(out);
    send("verify", 0, 1);
    if n != t.size || got != want {
        return Err("The download arrived damaged. Nothing was changed. Try again.".into());
    }
    send("verify", 1, 1);

    send("extract", 0, 1);
    extract(work, &t.asset, "staged", &st.cancel)?;
    send("extract", 1, 1);
    let version = t.tag.trim_start_matches(['v', 'V']);
    let package = find_package(&work.join("staged"), version, PACKAGE_LAUNCHER)?;

    // the last moment to turn back: the download took minutes and Steam can start the game without the launcher
    if st.cancel.load(SeqCst) {
        return Err(CANCELLED.into());
    }
    ready()?;
    st.applying.store(true, SeqCst);
    let job = Job { root: &p.install, package: &package, work, launcher_name: PACKAGE_LAUNCHER, launcher: &p.launcher, game_dll: game_dll(p) };
    let applied = apply(&job, &mut send)?;
    // macOS: the package carries the runtime dylib twice (beside the launcher and in tools/build/sdk), so the player's copy
    // follows the one the update replaced. Nothing is signed: the release zip is unsigned by design.
    #[cfg(target_os = "macos")]
    if let Err(e) = sync_runtime_lib(&p.install) {
        let failed = applied.undo();
        return Err(if failed.is_empty() { format!("{e} Nothing was changed.") } else { format!("{e} {}", rollback_message(&failed)) });
    }
    Ok(applied)
}

/// The package carries the runtime dylib twice: beside the launcher, and in tools/build/sdk, which is where Set up copies it
/// from into the game folder. Bring the copies already on the player's disk along with the one the update replaced (an install
/// made from an older bundle has the second one in Contents/Frameworks).
#[cfg_attr(not(target_os = "macos"), allow(dead_code))] // type-checked on every host
fn sync_runtime_lib(root: &Path) -> Result<(), String> {
    let Some(parent) = root.parent() else { return Ok(()) };
    let src = root.join("tools").join("build").join("sdk").join("libpkp_rt.dylib");
    if !src.is_file() {
        return Ok(());
    }
    for dst in [root.join("libpkp_rt.dylib"), parent.join("Frameworks").join("libpkp_rt.dylib")] {
        if dst.is_file() {
            fs::copy(&src, &dst).map_err(|e| format!("Could not update the runtime library in {} ({e}).", dst.display()))?;
        }
    }
    Ok(())
}

/// Starts the launcher now at `path` with `--updated`. host::command keeps a new AppImage from inheriting the old one's mount. A launcher
/// run with --appimage-extract-and-run (no FUSE) has no mount either, and the new one needs the same way in: the variable says it.
pub fn relaunch(path: &Path, dir: &Path) -> io::Result<()> {
    let mut cmd = crate::host::command(path);
    #[cfg(not(windows))]
    if std::env::var("APPDIR").is_ok_and(|d| !d.is_empty() && !d.contains(".mount_")) {
        cmd.env("APPIMAGE_EXTRACT_AND_RUN", "1");
    }
    cmd.arg("--updated").current_dir(dir).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null());
    cmd.spawn().map(drop)
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_runtime_library_follows_the_one_the_update_replaced() {
        // both copies the package ships: beside the launcher, and the one in Contents/Frameworks an older bundle install has
        let tmp = std::env::temp_dir().join(format!("nxrt-runtime-lib-{}", std::process::id()));
        let root = tmp.join("PokopiaRecomp-0.12.0");
        let frameworks = tmp.join("Frameworks");
        fs::create_dir_all(root.join("tools").join("build").join("sdk")).unwrap();
        fs::create_dir_all(&frameworks).unwrap();
        let (new, beside, in_frameworks) =
            (root.join("tools/build/sdk/libpkp_rt.dylib"), root.join("libpkp_rt.dylib"), frameworks.join("libpkp_rt.dylib"));
        fs::write(&new, b"new").unwrap();
        fs::write(&beside, b"old").unwrap();
        fs::write(&in_frameworks, b"old").unwrap();
        assert!(sync_runtime_lib(&root).is_ok());
        assert_eq!(fs::read(&beside).unwrap(), b"new", "the copy beside the launcher is the updated one");
        assert_eq!(fs::read(&in_frameworks).unwrap(), b"new", "and so is an older bundle install's copy");
        let _ = fs::remove_dir_all(&tmp);
    }
}
