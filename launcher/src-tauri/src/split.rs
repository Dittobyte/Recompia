// SPDX-License-Identifier: GPL-3.0-or-later
//! Split screen: two to four players on this PC, each in a window of their own (the runtime's [split] keys,
//! runtime/include/nxrt/split.h).
//!
//! Play starts the game once per player. The first window is the active profile and hosts; every other one is another profile and
//! visits it through Link Play on 127.0.0.1. Nothing is written to pkp.ini: each window gets its role in its environment, which the
//! runtime reads before the file. A visiting player is a profile like any other (profiles.rs), so their save is a folder of their
//! own in the same save folder. One who has no save yet starts from a copy of the first player's, because a new game cannot visit
//! before its story has opened Link Play; the runtime gives that copy a player UUID of its own (the host refuses a visitor with its
//! own, and two copies must differ from each other too).

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;
use std::process::Command;

use crate::profiles::{self, Profile, Store};
use crate::saveimport;
use crate::settings::CONTROLS_INI;

/// The group's own port, so it does not meet a normal Link Play host on this PC (47600) or a relay (47610).
pub const PORT: u16 = 47620;
/// The visiting windows' own files, inside the game folder: `split/<window>/` holds that window's log and its copy of controls.ini.
const DIR: &str = "split";
/// The game's own limit for one Link Play session, and so the most windows.
pub const MAX_PLAYERS: usize = 4;

/// What the page asks for. Every field has the default a first start uses.
#[derive(Deserialize, Clone, Debug)]
#[serde(default)]
pub struct Options {
    /// How many play: 2 to 4 (anything else is 2).
    pub players: usize,
    /// Two players: left and right halves, else top and bottom. Three: the first player has the left half, else the top half.
    pub side: bool,
    /// The first player plays with the keyboard and mouse, so the others get the controllers from the first one on.
    pub keyboard: bool,
    /// The visiting windows play their sound too.
    pub sound: bool,
    /// The profile ids of players 2, 3 and 4, in that order. A missing or empty one: the next profile that can be one, made when
    /// there is none.
    pub others: Vec<String>,
    /// The players come together on their own: the first window opens its session and the others join it, once each player is in
    /// the world (the runtime's [split] meet). Off: the players meet through the game's Link Play menu.
    pub meet: bool,
    /// With meet: a player who joined starts where the first player stands, not where the game lets a visitor arrive (the
    /// runtime's [split] together). For the part of the game in which the first player cannot be reached from there.
    pub together: bool,
}

impl Default for Options {
    fn default() -> Self {
        Options { players: 2, side: false, keyboard: false, sound: false, others: Vec::new(), meet: true, together: true }
    }
}

impl Options {
    fn players(&self) -> usize {
        if (2..=MAX_PLAYERS).contains(&self.players) { self.players } else { 2 }
    }
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

/// The windows of one start: the first player's, and the visiting ones in player order.
pub struct Group {
    pub first: Window,
    pub others: Vec<Window>,
}

/// Who can visit: any profile but the active one and the first one. The first profile is the existing save with the runtime's
/// built-in user, which has no id of its own to hand to another window.
pub fn candidates(s: &Store) -> Vec<&Profile> {
    s.profiles.iter().filter(|p| p.id != s.active && p.id != profiles::DEFAULT_ID).collect()
}

/// What the page shows before a start: the first player, and who can be one of the others.
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

const NO_PROFILE: &str = "A player's profile could not be made.";

/// The visiting players' profiles, for players 2 to `players`: the ones asked for, then for every player left open the next
/// candidate nobody has, then a new profile ("Player 2", or the next free number). Nobody plays twice.
fn other_profiles(game_dir: &Path, players: usize, wanted: &[String]) -> Result<Vec<Profile>, String> {
    let mut s = profiles::load(game_dir);
    let mut out: Vec<Option<Profile>> = vec![None; players - 1];
    for (slot, id) in out.iter_mut().zip(wanted).filter(|(_, id)| !id.is_empty()) {
        let p = candidates(&s).into_iter().find(|p| p.id == *id).cloned().ok_or("That profile cannot be one of the other players.")?;
        *slot = Some(p);
    }
    let taken = |out: &[Option<Profile>], id: &str| out.iter().flatten().any(|p| p.id == id);
    for i in 0..out.len() {
        if out[i].is_some() {
            if out[..i].iter().flatten().any(|p| p.id == out[i].as_ref().unwrap().id) {
                return Err("The same profile cannot play in two windows.".into());
            }
            continue;
        }
        let free = candidates(&s).into_iter().find(|p| !taken(&out, &p.id)).cloned();
        out[i] = Some(match free {
            Some(p) => p,
            None => {
                let name = (2..100)
                    .map(|n| format!("Player {}", n))
                    .find(|n| !s.profiles.iter().any(|p| p.name.eq_ignore_ascii_case(n)))
                    .ok_or(NO_PROFILE)?;
                s = profiles::add(game_dir, &name)?;
                s.profiles.last().cloned().ok_or(NO_PROFILE)?
            }
        });
    }
    Ok(out.into_iter().flatten().collect())
}

/// The nickname as a window's environment can carry it. Windows hands the runtime its environment in the ANSI code page, which
/// loses a name outside it; such a name plays as "Player <n>" in split screen.
fn env_nickname(p: &Profile, player: usize) -> String {
    if p.nickname.is_empty() || (cfg!(windows) && !p.nickname.is_ascii()) {
        format!("Player {}", player)
    } else {
        p.nickname.clone()
    }
}

/// What every window of the group gets: how the screen is shared, and Link Play on this PC only.
fn shared_env(opt: &Options) -> Vec<(&'static str, String)> {
    vec![
        ("NXRT_SPLIT_COUNT", opt.players().to_string()),
        ("NXRT_SPLIT_LAYOUT", if opt.side { "side" } else { "stacked" }.into()),
        ("NXRT_SPLIT_MEET", opt.meet.to_string()),
        ("NXRT_SPLIT_TOGETHER", (opt.meet && opt.together).to_string()),
        ("NXRT_NET_PROTOCOL", "native".into()),
        ("NXRT_NET_INTERNET", "false".into()),
    ]
}

/// The first window: the active profile, as pkp.ini has it. It hosts on the group's port.
fn first_window(opt: &Options) -> Window {
    let mut env = shared_env(opt);
    env.extend([
        ("NXRT_SPLIT_WINDOW", "1".to_string()),
        ("NXRT_SPLIT_PAD", if opt.keyboard { "0" } else { "1" }.to_string()),
        ("NXRT_NET_MODE", format!("auto:{}", PORT)),
    ]);
    Window { log: "pkp.log".into(), env }
}

/// A visiting window (`player` 2 to 4): the player's account and Link Play identity, the first player's account to differ from (its
/// folder name: Windows hands the runtime its environment in the ANSI code page, which does not carry every path) and files of its
/// own for what a running game writes next to itself. It only reads the pipeline cache: several processes writing one file would race.
fn other_window(opt: &Options, player: usize, profile: &Profile, first_key: &str) -> Window {
    let mut env = shared_env(opt);
    env.extend([
        ("NXRT_SPLIT_WINDOW", player.to_string()),
        // with the first player on the keyboard, the controllers start at the second player
        ("NXRT_SPLIT_PAD", (if opt.keyboard { player - 1 } else { player }).to_string()),
        ("NXRT_SPLIT_MUTE", if opt.sound { "false" } else { "true" }.to_string()),
        ("NXRT_SPLIT_FIRST_ACCOUNT", first_key.to_string()),
        ("NXRT_ACCOUNT_UID", profile.uid.clone()),
        ("NXRT_ACCOUNT_NICKNAME", env_nickname(profile, player)),
        ("NXRT_NET_UID", profile.net_uid.clone()),
        ("NXRT_NET_MODE", format!("join:127.0.0.1:{}", PORT)),
        ("NXRT_PLAY_CONTROLS", format!("{}/{}/{}", DIR, player, CONTROLS_INI)),
        ("NXRT_NVN_PIPELINE_CACHE_SAVE", "off".to_string()),
    ]);
    Window { log: format!("{}/{}/pkp.log", DIR, player), env }
}

/// The first player's account folder: the one of the active profile's uid. The first profile's save can be older than its uid (a save
/// from before pkp.ini named one): then it is `found`, the folder the launcher's other save tools use (saveimport::locate), unless
/// that is one of the other players'.
fn first_key(s: &Store, account: &Path, found: String, other_keys: &[String]) -> String {
    let p = s.active_profile();
    let own = saveimport::account_key_of(&p.uid);
    if p.id != profiles::DEFAULT_ID || saveimport::is_save_folder(&account.join(&own)) || other_keys.contains(&found) {
        return own;
    }
    found
}

/// Everything a split start needs before a process runs: the other players' profiles, their saves (a copy of the first player's for
/// one who has none) and each visiting window's copy of controls.ini. The caller has checked that the game is not running.
pub fn prepare(game_dir: &Path, opt: &Options) -> Result<Group, String> {
    let profiles = other_profiles(game_dir, opt.players(), &opt.others)?;
    let (save_dir, found_key) = saveimport::locate(game_dir);
    let keys = profiles.iter().map(|p| saveimport::uid_key(&p.uid).ok_or(NO_PROFILE.to_string())).collect::<Result<Vec<_>, _>>()?;
    let account = save_dir.join("account");
    let first_key = first_key(&profiles::load(game_dir), &account, found_key, &keys);
    let first_save = account.join(&first_key);
    for key in keys.iter().filter(|k| !account.join(k).exists()) {
        if !saveimport::is_save_folder(&first_save) {
            break;   // no save to start from: these players begin a new game
        }
        // the runtime's own rule (fs_volume.cpp): with <key>.commit beside it the folder is not one consistent save
        if account.join(format!("{}.commit", first_key)).exists() {
            return Err("Your save has a write the game did not finish. Start the game once without split screen, then try again.".into());
        }
        saveimport::copy_account(&save_dir, &first_key, key).map_err(|e| format!("A player's save could not be made: {}", e))?;
    }

    // A visiting window starts every time from the settings as they are now: what is changed in it lasts for that session.
    let not_prepared = |e: std::io::Error| format!("A window's settings could not be prepared: {}", e);
    let mut others = vec![];
    for (i, profile) in profiles.iter().enumerate() {
        let player = i + 2;
        let own = game_dir.join(DIR).join(player.to_string());
        fs::create_dir_all(&own).map_err(not_prepared)?;
        let (from, to) = (game_dir.join(CONTROLS_INI), own.join(CONTROLS_INI));
        if from.is_file() {
            fs::copy(&from, &to).map_err(not_prepared)?;
        } else {
            let _ = fs::remove_file(&to);   // no settings yet: the window writes the factory ones, as the first does
        }
        others.push(other_window(opt, player, profile, &first_key));
    }
    Ok(Group { first: first_window(opt), others })
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

    fn save_of(d: &Path, p: &Profile) -> std::path::PathBuf {
        d.join("save").join("account").join(saveimport::uid_key(&p.uid).unwrap())
    }

    #[test]
    fn a_first_start_makes_the_second_player_from_the_first_players_save() {
        let d = tmp("first");
        let first = first_save(&d);
        fs::write(d.join(CONTROLS_INI), "[display]\nwidth = 1600\n").unwrap();
        let group = prepare(&d, &Options::default()).unwrap();
        assert_eq!(group.others.len(), 1, "two players unless the page says more");
        let other = &group.others[0];

        let s = profiles::load(&d);
        let second = s.profiles.iter().find(|p| p.name == "Player 2").expect("a profile for the second player");
        assert_eq!(s.active, profiles::DEFAULT_ID, "the first player stays the active profile");
        assert_eq!(fs::read(save_of(&d, second).join("S00").join("SYDAT.BIN")).unwrap(), b"system");
        assert_eq!(fs::read(first.join("G00").join("CMNDAT.BIN")).unwrap(), b"common", "the first player's save is only read");
        assert_eq!(fs::read_to_string(d.join(DIR).join("2").join(CONTROLS_INI)).unwrap(), "[display]\nwidth = 1600\n");

        assert_eq!(env_of(&group.first, "NXRT_SPLIT_WINDOW"), Some("1"));
        assert_eq!(env_of(&group.first, "NXRT_SPLIT_COUNT"), Some("2"));
        assert_eq!(env_of(&group.first, "NXRT_SPLIT_PAD"), Some("1"));
        assert_eq!(env_of(&group.first, "NXRT_SPLIT_MEET"), Some("true"), "the players meet on their own unless the page says no");
        assert_eq!(env_of(other, "NXRT_SPLIT_TOGETHER"), Some("true"));
        assert_eq!(env_of(&group.first, "NXRT_NET_MODE"), Some("auto:47620"));
        assert_eq!(env_of(&group.first, "NXRT_ACCOUNT_UID"), None, "the first window plays the account in pkp.ini");
        assert_eq!(env_of(other, "NXRT_SPLIT_WINDOW"), Some("2"));
        assert_eq!(env_of(other, "NXRT_SPLIT_PAD"), Some("2"));
        assert_eq!(env_of(other, "NXRT_SPLIT_MUTE"), Some("true"));
        assert_eq!(env_of(other, "NXRT_NET_MODE"), Some("join:127.0.0.1:47620"));
        assert_eq!(env_of(other, "NXRT_ACCOUNT_UID"), Some(second.uid.as_str()));
        assert_eq!(env_of(other, "NXRT_NET_UID"), Some(second.net_uid.as_str()));
        assert_eq!(env_of(other, "NXRT_SPLIT_FIRST_ACCOUNT"), first.file_name().and_then(|n| n.to_str()));
        assert_eq!(other.log, "split/2/pkp.log");
    }

    #[test]
    fn a_second_start_keeps_the_second_players_save_and_profile() {
        let d = tmp("again");
        first_save(&d);
        prepare(&d, &Options::default()).unwrap();
        let s = profiles::load(&d);
        let second = s.profiles.iter().find(|p| p.name == "Player 2").unwrap().clone();
        let copy = save_of(&d, &second);
        fs::write(copy.join("S00").join("SYDAT.BIN"), b"their own progress").unwrap();

        let group = prepare(&d, &Options { side: true, keyboard: true, sound: true, meet: false, ..Default::default() }).unwrap();
        assert_eq!(profiles::load(&d).profiles.len(), 2, "no second Player 2");
        assert_eq!(fs::read(copy.join("S00").join("SYDAT.BIN")).unwrap(), b"their own progress");
        assert_eq!(env_of(&group.others[0], "NXRT_ACCOUNT_UID"), Some(second.uid.as_str()));
        // one-controller mode: the first player has the keyboard, the only controller is the second player's
        assert_eq!(env_of(&group.first, "NXRT_SPLIT_PAD"), Some("0"));
        assert_eq!(env_of(&group.others[0], "NXRT_SPLIT_PAD"), Some("1"));
        assert_eq!(env_of(&group.others[0], "NXRT_SPLIT_MUTE"), Some("false"));
        assert_eq!(env_of(&group.first, "NXRT_SPLIT_LAYOUT"), Some("side"));
        assert_eq!(env_of(&group.others[0], "NXRT_SPLIT_MEET"), Some("false"));
        assert_eq!(env_of(&group.others[0], "NXRT_SPLIT_TOGETHER"), Some("false"), "nobody is moved when the players meet by hand");
    }

    #[test]
    fn four_players_are_four_windows_with_a_profile_and_a_save_each() {
        let d = tmp("four");
        first_save(&d);
        let misty = profiles::add(&d, "Misty").unwrap().profiles.last().unwrap().clone();
        let group = prepare(&d, &Options { players: 4, keyboard: true, ..Default::default() }).unwrap();
        assert_eq!(group.others.len(), 3);

        let s = profiles::load(&d);
        let names: Vec<&str> = s.profiles.iter().map(|p| p.name.as_str()).collect();
        assert_eq!(names[1..], ["Misty", "Player 2", "Player 3"], "the profile that was there plays, two are made");
        assert_eq!(env_of(&group.others[0], "NXRT_ACCOUNT_UID"), Some(misty.uid.as_str()));
        let mut uids = vec![];
        for (i, w) in group.others.iter().enumerate() {
            let player = i + 2;
            assert_eq!(env_of(w, "NXRT_SPLIT_WINDOW"), Some(player.to_string().as_str()));
            assert_eq!(env_of(w, "NXRT_SPLIT_COUNT"), Some("4"));
            assert_eq!(env_of(w, "NXRT_SPLIT_PAD"), Some((player - 1).to_string().as_str()), "the keyboard is the first player's");
            assert_eq!(w.log, format!("split/{}/pkp.log", player));
            uids.push(env_of(w, "NXRT_ACCOUNT_UID").unwrap().to_string());
        }
        uids.sort();
        uids.dedup();
        assert_eq!(uids.len(), 3, "nobody plays twice");
        for p in &s.profiles[1..] {
            assert!(saveimport::is_save_folder(&save_of(&d, p)), "{} starts from a copy of the first save", p.name);
        }
        // a third start with one player fewer leaves the profiles as they are
        prepare(&d, &Options { players: 3, ..Default::default() }).unwrap();
        assert_eq!(profiles::load(&d).profiles.len(), 4);
    }

    #[test]
    fn the_page_can_say_who_plays_where() {
        let d = tmp("pick");
        first_save(&d);
        let misty = profiles::add(&d, "Misty").unwrap().profiles.last().unwrap().clone();
        let brock = profiles::add(&d, "Brock").unwrap().profiles.last().unwrap().clone();
        let group = prepare(&d, &Options { players: 3, others: vec![brock.id.clone(), misty.id.clone()], ..Default::default() }).unwrap();
        assert_eq!(env_of(&group.others[0], "NXRT_ACCOUNT_UID"), Some(brock.uid.as_str()));
        assert_eq!(env_of(&group.others[1], "NXRT_ACCOUNT_UID"), Some(misty.uid.as_str()));
        // only player 3 is said: player 2 is the candidate that is left
        let group = prepare(&d, &Options { players: 3, others: vec![String::new(), misty.id.clone()], ..Default::default() }).unwrap();
        assert_eq!(env_of(&group.others[0], "NXRT_ACCOUNT_UID"), Some(brock.uid.as_str()));
        assert!(prepare(&d, &Options { players: 3, others: vec![misty.id.clone(), misty.id.clone()], ..Default::default() }).is_err());
    }

    #[test]
    fn only_another_profile_with_its_own_id_can_visit() {
        let d = tmp("who");
        first_save(&d);
        let s = profiles::add(&d, "Misty").unwrap();
        let misty = s.profiles.last().unwrap().clone();
        assert_eq!(players(&d).candidates.iter().map(|c| c.name.as_str()).collect::<Vec<_>>(), ["Misty"]);
        // the first profile is the built-in user: never a visitor, also while another profile is active
        profiles::select(&d, &misty.id).unwrap();
        assert!(players(&d).candidates.is_empty());
        assert!(prepare(&d, &Options { others: vec![profiles::DEFAULT_ID.into()], ..Default::default() }).is_err());
        assert!(prepare(&d, &Options { others: vec![misty.id.clone()], ..Default::default() }).is_err(), "the active profile is the first player");
    }

    #[test]
    fn a_player_without_a_first_save_starts_a_new_game() {
        let d = tmp("new");
        let group = prepare(&d, &Options::default()).unwrap();
        let s = profiles::load(&d);
        let second = s.profiles.last().unwrap();
        assert!(!save_of(&d, second).exists());
        assert_eq!(env_of(&group.others[0], "NXRT_SPLIT_WINDOW"), Some("2"));
        assert!(!d.join(DIR).join("2").join(CONTROLS_INI).exists());
    }

    #[test]
    fn an_unfinished_write_in_the_first_save_stops_the_copy() {
        let d = tmp("commit");
        let first = first_save(&d);
        fs::write(first.with_file_name(format!("{}.commit", first.file_name().unwrap().to_string_lossy())), b"").unwrap();
        assert!(prepare(&d, &Options::default()).is_err());
    }
}
