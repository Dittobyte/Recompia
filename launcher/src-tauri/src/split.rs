// SPDX-License-Identifier: GPL-3.0-or-later
//! Split screen: two players on this PC, each in a window of their own (the runtime's [split] keys, runtime/include/nxrt/split.h).
//!
//! Play starts the game twice. The first window is the active profile and hosts; the second is another profile and visits it through
//! Link Play on 127.0.0.1. Nothing is written to pkp.ini: each window gets its role in its environment, which the runtime reads
//! before the file. The second player is a profile like any other (profiles.rs), so their save is a folder of their own in the same
//! save folder. One who has no save yet starts from a copy of the first player's, because a new game cannot visit before its story
//! has opened Link Play; the runtime gives that copy a player UUID of its own (the host refuses a visitor with its own).

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;
use std::process::Command;

use crate::profiles::{self, Profile, Store};
use crate::saveimport;
use crate::settings::CONTROLS_INI;

/// The pair's own port, so it does not meet a normal Link Play host on this PC (47600) or a relay (47610).
pub const PORT: u16 = 47620;
/// The second window's own files, inside the game folder: its log and its copy of controls.ini.
const DIR: &str = "split";
const SECOND_NAME: &str = "Player 2";

/// What the page asks for. Every field has the default a first start uses.
#[derive(Deserialize, Clone, Debug, Default)]
#[serde(default)]
pub struct Options {
    /// Left and right halves; else top and bottom.
    pub side: bool,
    /// The first player plays with the keyboard and mouse, so the second gets the first controller.
    pub keyboard: bool,
    /// The second window plays its sound too.
    pub sound: bool,
    /// The second player's profile id. Empty: the first profile that can be one, made when there is none.
    pub second: String,
}

/// One window: its log file (relative to the game folder) and what its environment adds.
pub struct Window {
    pub log: String,
    env: Vec<(&'static str, String)>,
}

impl Window {
    pub fn apply(&self, cmd: &mut Command) {
        for (k, v) in &self.env {
            cmd.env(k, v);
        }
    }
}

pub struct Pair {
    pub first: Window,
    pub second: Window,
}

/// Who can be the second player: any profile but the active one and the first one. The first profile is the existing save with the
/// runtime's built-in user, which has no id of its own to hand to a second window.
pub fn candidates(s: &Store) -> Vec<&Profile> {
    s.profiles.iter().filter(|p| p.id != s.active && p.id != profiles::DEFAULT_ID).collect()
}

/// What the page shows before a start: who would be the second player.
#[derive(Serialize)]
pub struct Players {
    pub first: String,
    pub candidates: Vec<Candidate>,
}

#[derive(Serialize)]
pub struct Candidate {
    pub id: String,
    pub name: String,
}

pub fn players(game_dir: &Path) -> Players {
    let s = profiles::load(game_dir);
    Players {
        first: s.active_profile().name.clone(),
        candidates: candidates(&s).into_iter().map(|p| Candidate { id: p.id.clone(), name: p.name.clone() }).collect(),
    }
}

/// The second player's profile: the one asked for, else the first candidate, else a new profile ("Player 2", or the next free number).
fn second_profile(game_dir: &Path, wanted: &str) -> Result<Profile, String> {
    let s = profiles::load(game_dir);
    let list = candidates(&s);
    if !wanted.is_empty() {
        return list.into_iter().find(|p| p.id == wanted).cloned().ok_or_else(|| "That profile cannot be the second player.".to_string());
    }
    if let Some(p) = list.first() {
        return Ok((*p).clone());
    }
    let name = (2..100)
        .map(|n| if n == 2 { SECOND_NAME.to_string() } else { format!("Player {}", n) })
        .find(|n| !s.profiles.iter().any(|p| p.name.eq_ignore_ascii_case(n)))
        .ok_or("The second player's profile could not be made.")?;
    let added = profiles::add(game_dir, &name)?;
    added.profiles.last().cloned().ok_or_else(|| "The second player's profile could not be made.".to_string())
}

/// The nickname as the second window's environment can carry it. Windows hands the runtime its environment in the ANSI code page,
/// which loses a name outside it; such a name plays as "Player 2" in split screen.
fn env_nickname(p: &Profile) -> String {
    if p.nickname.is_empty() || (cfg!(windows) && !p.nickname.is_ascii()) {
        SECOND_NAME.to_string()
    } else {
        p.nickname.clone()
    }
}

fn layout(opt: &Options) -> String {
    if opt.side { "side" } else { "stacked" }.to_string()
}

/// The first window: the active profile, as pkp.ini has it. It hosts on the pair's port.
fn first_window(opt: &Options) -> Window {
    Window {
        log: "pkp.log".into(),
        env: vec![
            ("NXRT_SPLIT_WINDOW", "1".into()),
            ("NXRT_SPLIT_LAYOUT", layout(opt)),
            ("NXRT_SPLIT_PAD", if opt.keyboard { "0" } else { "1" }.into()),
            ("NXRT_NET_PROTOCOL", "native".into()),
            ("NXRT_NET_INTERNET", "false".into()),
            ("NXRT_NET_MODE", format!("auto:{}", PORT)),
        ],
    }
}

/// The second window: the second player's account and Link Play identity, the first player's account to differ from (its folder name:
/// Windows hands the runtime its environment in the ANSI code page, which does not carry every path) and files of its own for what a
/// running game writes next to itself. It only reads the pipeline cache: two processes writing one file would race.
fn second_window(opt: &Options, second: &Profile, first_key: &str) -> Window {
    Window {
        log: format!("{}/pkp.log", DIR),
        env: vec![
            ("NXRT_SPLIT_WINDOW", "2".into()),
            ("NXRT_SPLIT_LAYOUT", layout(opt)),
            ("NXRT_SPLIT_PAD", if opt.keyboard { "1" } else { "2" }.into()),
            ("NXRT_SPLIT_MUTE", if opt.sound { "false" } else { "true" }.into()),
            ("NXRT_SPLIT_FIRST_ACCOUNT", first_key.to_string()),
            ("NXRT_ACCOUNT_UID", second.uid.clone()),
            ("NXRT_ACCOUNT_NICKNAME", env_nickname(second)),
            ("NXRT_NET_UID", second.net_uid.clone()),
            ("NXRT_NET_PROTOCOL", "native".into()),
            ("NXRT_NET_INTERNET", "false".into()),
            ("NXRT_NET_MODE", format!("join:127.0.0.1:{}", PORT)),
            ("NXRT_PLAY_CONTROLS", format!("{}/{}", DIR, CONTROLS_INI)),
            ("NXRT_NVN_PIPELINE_CACHE_SAVE", "off".into()),
        ],
    }
}

/// The first player's account folder: the one of the active profile's uid. The first profile's save can be older than its uid (a save
/// from before pkp.ini named one): then it is `found`, the folder the launcher's other save tools use (saveimport::locate), unless
/// that is the second player's.
fn first_key(s: &Store, account: &Path, found: String, second_key: &str) -> String {
    let p = s.active_profile();
    let own = saveimport::account_key_of(&p.uid);
    if p.id != profiles::DEFAULT_ID || saveimport::is_save_folder(&account.join(&own)) || found == second_key {
        return own;
    }
    found
}

/// Everything a split start needs before a process runs: the second player's profile, their save (a copy of the first player's when
/// they have none) and the second window's copy of controls.ini. The caller has checked that the game is not running.
pub fn prepare(game_dir: &Path, opt: &Options) -> Result<Pair, String> {
    let second = second_profile(game_dir, &opt.second)?;
    let (save_dir, found_key) = saveimport::locate(game_dir);
    let second_key = saveimport::uid_key(&second.uid).ok_or("The second player's profile could not be made.")?;
    let account = save_dir.join("account");
    let first_key = first_key(&profiles::load(game_dir), &account, found_key, &second_key);
    let first_save = account.join(&first_key);
    if !account.join(&second_key).exists() && saveimport::is_save_folder(&first_save) {
        // the runtime's own rule (fs_volume.cpp): with <key>.commit beside it the folder is not one consistent save
        if account.join(format!("{}.commit", first_key)).exists() {
            return Err("Your save has a write the game did not finish. Start the game once without split screen, then try again.".into());
        }
        saveimport::copy_account(&save_dir, &first_key, &second_key).map_err(|e| format!("The second player's save could not be made: {}", e))?;
    }

    // The second window starts every time from the settings as they are now: what is changed in it lasts for that session.
    let own = game_dir.join(DIR);
    let not_prepared = |e: std::io::Error| format!("The second window's settings could not be prepared: {}", e);
    fs::create_dir_all(&own).map_err(not_prepared)?;
    let (from, to) = (game_dir.join(CONTROLS_INI), own.join(CONTROLS_INI));
    if from.is_file() {
        fs::copy(&from, &to).map_err(not_prepared)?;
    } else {
        let _ = fs::remove_file(&to);   // no settings yet: the second window writes the factory ones, as the first does
    }
    Ok(Pair { first: first_window(opt), second: second_window(opt, &second, &first_key) })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::settings::PKP_INI;

    fn tmp(tag: &str) -> std::path::PathBuf {
        static N: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
        let d = std::env::temp_dir().join(format!("pkp-split-{}-{}-{}", tag, std::process::id(), N.fetch_add(1, std::sync::atomic::Ordering::SeqCst)));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(&d).unwrap();
        fs::write(d.join(PKP_INI), "[fs]\nsave_dir = save\n").unwrap();
        d
    }

    fn env_of<'a>(w: &'a Window, key: &str) -> Option<&'a str> {
        w.env.iter().find(|(k, _)| *k == key).map(|(_, v)| v.as_str())
    }

    /// A save of the first (built-in) user, the way the game leaves it.
    fn first_save(d: &Path) -> std::path::PathBuf {
        let root = d.join("save").join("account").join(saveimport::account_key_of(""));
        for part in ["G00", "S00"] {
            fs::create_dir_all(root.join(part)).unwrap();
        }
        fs::write(root.join("G00").join("CMNDAT.BIN"), b"common").unwrap();
        fs::write(root.join("S00").join("SYDAT.BIN"), b"system").unwrap();
        root
    }

    #[test]
    fn a_first_start_makes_the_second_player_from_the_first_players_save() {
        let d = tmp("first");
        let first = first_save(&d);
        fs::write(d.join(CONTROLS_INI), "[display]\nwidth = 1600\n").unwrap();
        let pair = prepare(&d, &Options::default()).unwrap();

        let s = profiles::load(&d);
        let second = s.profiles.iter().find(|p| p.name == SECOND_NAME).expect("a profile for the second player");
        assert_eq!(s.active, profiles::DEFAULT_ID, "the first player stays the active profile");
        let copy = d.join("save").join("account").join(saveimport::uid_key(&second.uid).unwrap());
        assert_eq!(fs::read(copy.join("S00").join("SYDAT.BIN")).unwrap(), b"system");
        assert_eq!(fs::read(first.join("G00").join("CMNDAT.BIN")).unwrap(), b"common", "the first player's save is only read");
        assert_eq!(fs::read_to_string(d.join(DIR).join(CONTROLS_INI)).unwrap(), "[display]\nwidth = 1600\n");

        assert_eq!(env_of(&pair.first, "NXRT_SPLIT_WINDOW"), Some("1"));
        assert_eq!(env_of(&pair.first, "NXRT_SPLIT_PAD"), Some("1"));
        assert_eq!(env_of(&pair.first, "NXRT_NET_MODE"), Some("auto:47620"));
        assert_eq!(env_of(&pair.first, "NXRT_ACCOUNT_UID"), None, "the first window plays the account in pkp.ini");
        assert_eq!(env_of(&pair.second, "NXRT_SPLIT_WINDOW"), Some("2"));
        assert_eq!(env_of(&pair.second, "NXRT_SPLIT_PAD"), Some("2"));
        assert_eq!(env_of(&pair.second, "NXRT_SPLIT_MUTE"), Some("true"));
        assert_eq!(env_of(&pair.second, "NXRT_NET_MODE"), Some("join:127.0.0.1:47620"));
        assert_eq!(env_of(&pair.second, "NXRT_ACCOUNT_UID"), Some(second.uid.as_str()));
        assert_eq!(env_of(&pair.second, "NXRT_NET_UID"), Some(second.net_uid.as_str()));
        assert_eq!(env_of(&pair.second, "NXRT_SPLIT_FIRST_ACCOUNT"), first.file_name().and_then(|n| n.to_str()));
        assert_eq!(pair.second.log, "split/pkp.log");
    }

    #[test]
    fn a_second_start_keeps_the_second_players_save_and_profile() {
        let d = tmp("again");
        first_save(&d);
        prepare(&d, &Options::default()).unwrap();
        let s = profiles::load(&d);
        let second = s.profiles.iter().find(|p| p.name == SECOND_NAME).unwrap().clone();
        let copy = d.join("save").join("account").join(saveimport::uid_key(&second.uid).unwrap());
        fs::write(copy.join("S00").join("SYDAT.BIN"), b"their own progress").unwrap();

        let pair = prepare(&d, &Options { side: true, keyboard: true, sound: true, second: String::new() }).unwrap();
        assert_eq!(profiles::load(&d).profiles.len(), 2, "no second Player 2");
        assert_eq!(fs::read(copy.join("S00").join("SYDAT.BIN")).unwrap(), b"their own progress");
        assert_eq!(env_of(&pair.second, "NXRT_ACCOUNT_UID"), Some(second.uid.as_str()));
        // one-controller mode: the first player has the keyboard, the only controller is the second player's
        assert_eq!(env_of(&pair.first, "NXRT_SPLIT_PAD"), Some("0"));
        assert_eq!(env_of(&pair.second, "NXRT_SPLIT_PAD"), Some("1"));
        assert_eq!(env_of(&pair.second, "NXRT_SPLIT_MUTE"), Some("false"));
        assert_eq!(env_of(&pair.first, "NXRT_SPLIT_LAYOUT"), Some("side"));
    }

    #[test]
    fn only_another_profile_with_its_own_id_can_be_second() {
        let d = tmp("who");
        first_save(&d);
        let s = profiles::add(&d, "Misty").unwrap();
        let misty = s.profiles.last().unwrap().clone();
        assert_eq!(players(&d).candidates.iter().map(|c| c.name.as_str()).collect::<Vec<_>>(), ["Misty"]);
        // the first profile is the built-in user: never the second player, also while another profile is active
        profiles::select(&d, &misty.id).unwrap();
        assert!(players(&d).candidates.is_empty());
        assert!(prepare(&d, &Options { second: profiles::DEFAULT_ID.into(), ..Default::default() }).is_err());
        assert!(prepare(&d, &Options { second: misty.id.clone(), ..Default::default() }).is_err(), "the active profile is the first player");
    }

    #[test]
    fn a_second_player_without_a_first_save_starts_a_new_game() {
        let d = tmp("new");
        let pair = prepare(&d, &Options::default()).unwrap();
        let s = profiles::load(&d);
        let second = s.profiles.last().unwrap();
        assert!(!d.join("save").join("account").join(saveimport::uid_key(&second.uid).unwrap()).exists());
        assert_eq!(env_of(&pair.second, "NXRT_SPLIT_WINDOW"), Some("2"));
        assert!(!d.join(DIR).join(CONTROLS_INI).exists());
    }

    #[test]
    fn an_unfinished_write_in_the_first_save_stops_the_copy() {
        let d = tmp("commit");
        let first = first_save(&d);
        fs::write(first.with_file_name(format!("{}.commit", first.file_name().unwrap().to_string_lossy())), b"").unwrap();
        assert!(prepare(&d, &Options::default()).is_err());
    }
}
