// SPDX-License-Identifier: GPL-3.0-or-later
//! Profiles: several players, each with their own save, on one install (#282).
//!
//! The game keeps an account save at <save_dir>/account/<uid>/, and the uid comes from [account] uid in pkp.ini. A profile is a name
//! plus the uid, nickname and Link Play uid ([net] uid) to write there, so a second profile is a second save folder inside the same
//! save folder. The dump, the shaders and the build are shared. `profiles.json` sits next to pkp.ini and is kept by updates.
//!
//! Nothing changes for a player who never adds a profile: without profiles.json no key is touched. The first profile ("default")
//! captures what pkp.ini holds today, so the existing save stays the default profile's. A profile is never deleted together with
//! its save: removing it only drops the entry, the save folder stays on disk.

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;

use crate::ini::Ini;
use crate::settings::{write_atomic, PKP_INI};

pub const FILE: &str = "profiles.json";
pub const DEFAULT_ID: &str = "default";
const MAX_NAME: usize = 24;
/// nn::account::Nickname holds 32 bytes.
const MAX_NICK_BYTES: usize = 32;
/// The runtime's built-in user (saveimport.rs DEFAULT_UID): a new profile must never get it.
const BUILT_IN_UID: &str = "000000000000000141434e4852656370";

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Member {
    pub name: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub net_uid: String,
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub role: String,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Coop {
    /// Private room code on the relay (XXXX-XXXX-XXXX-XXXX, Crockford base32), never listed publicly.
    pub room_code: String,
    /// Optional custom relay address (<IPv4>[:<port>]); empty means the public relay.
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub relay: String,
    #[serde(default)]
    pub members: Vec<Member>,
    #[serde(default)]
    pub world_version: u64,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Profile {
    pub id: String,
    pub name: String,
    /// [account] uid: 32 hex digits, or empty for the runtime's built-in user
    pub uid: String,
    /// [account] nickname, empty for the runtime's default
    pub nickname: String,
    /// [net] uid (Link Play identity): 32 hex digits, or empty to keep the one in the save folder
    pub net_uid: String,
    /// Present when this profile is a co-op save (own folder, launched in co-op mode only).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub coop: Option<Coop>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Store {
    pub active: String,
    pub profiles: Vec<Profile>,
}

impl Store {
    pub fn get(&self, id: &str) -> Option<&Profile> {
        self.profiles.iter().find(|p| p.id == id)
    }
    pub fn active_profile(&self) -> &Profile {
        self.get(&self.active).unwrap_or(&self.profiles[0])
    }
}

fn load_ini(dir: &Path) -> Ini {
    fs::read(dir.join(PKP_INI)).map(|b| Ini::parse(&String::from_utf8_lossy(&b))).unwrap_or_else(|_| Ini::empty())
}

/// The profile that stands for what pkp.ini holds now: the existing save.
fn default_profile(ini: &Ini) -> Profile {
    let get = |s: &str, k: &str| ini.get(s, k).unwrap_or_default().trim().to_string();
    // the runtime's own default nickname is "Player" (games/pkp/core/pkp_core.cpp); an empty key would give the game an empty name
    let nickname = Some(get("account", "nickname")).filter(|n| !n.is_empty()).unwrap_or_else(|| "Player".into());
    Profile {
        id: DEFAULT_ID.into(),
        name: nickname.chars().take(MAX_NAME).collect(),
        uid: get("account", "uid"),
        nickname,
        net_uid: get("net", "uid"),
        coop: None,
    }
}

/// True when the player has made profiles (profiles.json exists).
pub fn exists(dir: &Path) -> bool {
    dir.join(FILE).is_file()
}

/// The stored profiles, or the one default profile when there is no file yet (nothing is written by reading).
pub fn load(dir: &Path) -> Store {
    if let Some(s) = fs::read_to_string(dir.join(FILE)).ok().and_then(|t| serde_json::from_str::<Store>(&t).ok()) {
        if !s.profiles.is_empty() {
            return s;
        }
    }
    Store { active: DEFAULT_ID.into(), profiles: vec![default_profile(&load_ini(dir))] }
}

/// A profiles.json that `load` would not use (it does not read, does not parse, or lists no profile). Reading it falls back to the
/// default profile; writing over it would lose the names and ids that tie each profile to its save.
fn is_damaged(path: &Path) -> bool {
    path.is_file() && !fs::read_to_string(path).ok().and_then(|t| serde_json::from_str::<Store>(&t).ok()).map_or(false, |s| !s.profiles.is_empty())
}

/// Copies a damaged profiles.json aside (`profiles.json.damaged-<date-time>`, never an existing name) before it is replaced.
fn keep_damaged(dir: &Path) -> Result<(), String> {
    let path = dir.join(FILE);
    if !is_damaged(&path) {
        return Ok(());
    }
    let stamp = crate::fsutil::file_stamp();
    let target = (0..1000)
        .map(|i| dir.join(if i == 0 { format!("{}.damaged-{}", FILE, stamp) } else { format!("{}.damaged-{}-{}", FILE, stamp, i + 1) }))
        .find(|p| !p.exists())
        .ok_or("Too many damaged copies of profiles.json.")?;
    fs::copy(&path, &target).map_err(|e| format!("profiles.json is damaged and could not be copied aside ({}), so nothing was changed.", e))?;
    eprintln!("profiles.json was damaged: a copy is kept as {}.", target.display());
    Ok(())
}

fn save(dir: &Path, s: &Store) -> Result<(), String> {
    let text = serde_json::to_string_pretty(s).map_err(|e| e.to_string())?;
    keep_damaged(dir)?;
    write_atomic(&dir.join(FILE), &text)
}

/// 32 hex digits from the OS random source (the relay's).
fn random_hex() -> std::io::Result<String> {
    let mut bytes = [0u8; 16];
    crate::relay::random::os_random(&mut bytes)?;
    Ok(bytes.iter().map(|b| format!("{:02x}", b)).collect())
}

fn new_uid(taken: &[&str]) -> Result<String, String> {
    loop {
        let u = random_hex().map_err(|e| format!("Cannot make an id for the profile: {}", e))?;
        // a zero half is not a valid account uid for the game, and the built-in user belongs to the default
        if u[..16] != *"0000000000000000" && u[16..] != *"0000000000000000" && u != BUILT_IN_UID && !taken.iter().any(|t| t.eq_ignore_ascii_case(&u)) {
            return Ok(u);
        }
    }
}

/// A name a player can see in a list: trimmed, 1 to 24 characters, no control characters.
pub fn clean_name(raw: &str) -> Result<String, String> {
    let n = raw.trim();
    if n.is_empty() {
        return Err("Type a name for the profile.".into());
    }
    if n.chars().count() > MAX_NAME {
        return Err(format!("The name can have at most {} characters.", MAX_NAME));
    }
    if n.chars().any(|c| c.is_control()) {
        return Err("The name cannot have control characters.".into());
    }
    Ok(n.to_string())
}

fn nickname_of(name: &str) -> String {
    let mut out = String::new();
    for c in name.chars() {
        if out.len() + c.len_utf8() > MAX_NICK_BYTES {
            break;
        }
        out.push(c);
    }
    out
}

fn taken(s: &Store, name: &str, except: Option<&str>) -> bool {
    s.profiles.iter().any(|p| Some(p.id.as_str()) != except && p.name.eq_ignore_ascii_case(name))
}

pub fn add(dir: &Path, name: &str) -> Result<Store, String> {
    let mut s = load(dir);
    let p = make_profile(&s, name, None)?;
    s.profiles.push(p);
    save(dir, &s)?;
    Ok(s)
}

fn make_profile(s: &Store, name: &str, coop: Option<Coop>) -> Result<Profile, String> {
    let name = clean_name(name)?;
    if taken(s, &name, None) {
        return Err(format!("There is already a profile called {}.", name));
    }
    let uids: Vec<&str> = s.profiles.iter().flat_map(|p| [p.uid.as_str(), p.net_uid.as_str()]).collect();
    let (uid, net_uid) = (new_uid(&uids)?, new_uid(&uids)?);
    let id = format!("p{}", &uid[..8]);
    Ok(Profile { id, name: name.clone(), uid, nickname: nickname_of(&name), net_uid, coop })
}

fn make_coop(room_code: Option<&str>, relay: Option<&str>, creator_name: &str, net_uid: &str, world_version: u64) -> Result<Coop, String> {
    let room_code = match room_code.map(str::trim).filter(|c| !c.is_empty()) {
        Some(c) => crate::coop::normalize_room_code(c)?,
        None => crate::coop::new_room_code()?,
    };
    let relay = match relay {
        Some(r) => crate::coop::validate_relay(r)?,
        None => String::new(),
    };
    let members = vec![Member { name: creator_name.to_string(), net_uid: net_uid.to_string(), role: "host".into() }];
    Ok(Coop { room_code, relay, members, world_version })
}

/// Creates a new co-op profile with its own save folder, private room code and recorded group metadata.
pub fn add_coop(dir: &Path, name: &str, room_code: Option<&str>, relay: Option<&str>) -> Result<Store, String> {
    let mut s = load(dir);
    let mut p = make_profile(&s, name, None)?;
    p.coop = Some(make_coop(room_code, relay, &p.name, &p.net_uid, 0)?);
    s.profiles.push(p);
    save(dir, &s)?;
    Ok(s)
}

/// Account save folder key of `p`, falling back to `saveimport::locate` only for the default profile's legacy save folder.
fn account_key_for(save_dir: &Path, found_key: &str, p: &Profile) -> String {
    let own = crate::saveimport::account_key_of(&p.uid);
    if p.id != DEFAULT_ID || crate::saveimport::is_save_folder(&save_dir.join("account").join(&own)) {
        own
    } else {
        found_key.to_string()
    }
}

fn check_copy_source(save_dir: &Path, key: &str) -> Result<(), String> {
    let acc = save_dir.join("account");
    if !crate::saveimport::is_save_folder(&acc.join(key)) {
        return Err("That profile has no save to copy yet.".into());
    }
    if acc.join(format!("{}.commit", key)).exists() {
        return Err("That save has a write the game did not finish. Start the game once on that profile, then try again.".into());
    }
    Ok(())
}

/// "Start coop from my singleplayer save": creates a co-op profile from a copy of a singleplayer profile's save via saveimport::copy_account.
pub fn start_coop_from(dir: &Path, source_id: &str, name: &str, room_code: Option<&str>, relay: Option<&str>) -> Result<Store, String> {
    let mut s = load(dir);
    let src = s.get(source_id).ok_or("That profile does not exist.")?.clone();
    if src.coop.is_some() {
        return Err("Pick a singleplayer profile to start co-op from.".into());
    }
    let (save_dir, found_key) = crate::saveimport::locate(dir);
    let from_key = account_key_for(&save_dir, &found_key, &src);
    check_copy_source(&save_dir, &from_key)?;

    let mut p = make_profile(&s, name, None)?;
    p.coop = Some(make_coop(room_code, relay, &p.name, &p.net_uid, 1)?);
    let to_key = crate::saveimport::uid_key(&p.uid).ok_or("Cannot make an id for the profile.")?;
    crate::saveimport::copy_account(&save_dir, &from_key, &to_key)?;
    s.profiles.push(p);
    save(dir, &s)?;
    Ok(s)
}

/// "Copy to singleplayer": creates a singleplayer profile from a copy of a co-op profile's save via saveimport::copy_account.
pub fn copy_to_singleplayer(dir: &Path, source_id: &str, name: &str) -> Result<Store, String> {
    let mut s = load(dir);
    let src = s.get(source_id).ok_or("That profile does not exist.")?.clone();
    if src.coop.is_none() {
        return Err("That profile is already a singleplayer profile.".into());
    }
    let (save_dir, found_key) = crate::saveimport::locate(dir);
    let from_key = account_key_for(&save_dir, &found_key, &src);
    check_copy_source(&save_dir, &from_key)?;

    let p = make_profile(&s, name, None)?;
    let to_key = crate::saveimport::uid_key(&p.uid).ok_or("Cannot make an id for the profile.")?;
    crate::saveimport::copy_account(&save_dir, &from_key, &to_key)?;
    s.profiles.push(p);
    save(dir, &s)?;
    Ok(s)
}

/// Updates a co-op profile's private room code (Some("") regenerates a fresh code), relay address, or member list.
pub fn update_coop(dir: &Path, id: &str, room_code: Option<&str>, relay: Option<&str>, members: Option<Vec<Member>>) -> Result<Store, String> {
    let mut s = load(dir);
    let p = s.profiles.iter_mut().find(|p| p.id == id).ok_or("That profile does not exist.")?;
    let coop = p.coop.as_mut().ok_or("That profile is not a co-op profile.")?;
    if let Some(code) = room_code {
        coop.room_code = if code.trim().is_empty() {
            crate::coop::new_room_code()?
        } else {
            crate::coop::normalize_room_code(code)?
        };
    }
    if let Some(r) = relay {
        coop.relay = crate::coop::validate_relay(r)?;
    }
    if let Some(m) = members {
        coop.members = crate::coop::validate_members(m)?;
    }
    save(dir, &s)?;
    Ok(s)
}

/// Increments a co-op profile's world version after a session finishes.
pub fn bump_world_version(dir: &Path, id: &str) -> Result<Store, String> {
    let mut s = load(dir);
    let p = s.profiles.iter_mut().find(|p| p.id == id).ok_or("That profile does not exist.")?;
    let coop = p.coop.as_mut().ok_or("That profile is not a co-op profile.")?;
    coop.world_version = coop.world_version.saturating_add(1);
    save(dir, &s)?;
    Ok(s)
}

pub fn rename(dir: &Path, id: &str, name: &str) -> Result<Store, String> {
    let name = clean_name(name)?;
    let mut s = load(dir);
    if taken(&s, &name, Some(id)) {
        return Err(format!("There is already a profile called {}.", name));
    }
    let p = s.profiles.iter_mut().find(|p| p.id == id).ok_or("That profile does not exist.")?;
    // the nickname the game shows follows the name, except for the default profile's own nickname if the player set one
    if p.nickname.is_empty() || p.nickname == nickname_of(&p.name) {
        p.nickname = nickname_of(&name);
    }
    p.name = name;
    save(dir, &s)?;
    Ok(s)
}

/// Drops the entry only; its save folder stays on disk. The first profile (the existing save) cannot be removed.
pub fn remove(dir: &Path, id: &str) -> Result<Store, String> {
    let mut s = load(dir);
    if id == DEFAULT_ID {
        return Err("The first profile holds the existing save and cannot be removed.".into());
    }
    let before = s.profiles.len();
    s.profiles.retain(|p| p.id != id);
    if s.profiles.len() == before {
        return Err("That profile does not exist.".into());
    }
    if s.active == id {
        s.active = DEFAULT_ID.into();
    }
    save(dir, &s)?;
    apply_active(dir)?;
    Ok(s)
}

pub fn select(dir: &Path, id: &str) -> Result<Store, String> {
    let mut s = load(dir);
    if s.get(id).is_none() {
        return Err("That profile does not exist.".into());
    }
    s.active = id.to_string();
    save(dir, &s)?;
    apply_active(dir)?;
    Ok(s)
}

/// Writes the active singleplayer profile's uid, nickname and Link Play uid into pkp.ini.
/// Does nothing until the player has made a profile, and never writes a co-op profile's uid or settings into pkp.ini.
pub fn apply_active(dir: &Path) -> Result<(), String> {
    if !exists(dir) {
        return Ok(());
    }
    let s = load(dir);
    let p = s.active_profile().clone();
    if p.coop.is_some() {
        return Ok(());
    }
    let path = dir.join(PKP_INI);
    let mut ini = load_ini(dir);
    let mut changed = !path.exists();
    for (sec, key, val) in [("account", "uid", &p.uid), ("account", "nickname", &p.nickname), ("net", "uid", &p.net_uid)] {
        let cur = ini.get(sec, key).unwrap_or_default();
        // a key the profile leaves empty is not created, but one left by another profile is cleared
        if cur.trim() != val.as_str() && !(cur.trim().is_empty() && val.is_empty()) {
            ini.set(sec, key, val);
            changed = true;
        }
    }
    if changed {
        write_atomic(&path, &ini.to_text())?;
    }
    Ok(())
}

/// The save folder name the active profile plays, when it is not the first profile: its own uid, never "the newest save".
pub fn strict_save_key(dir: &Path) -> Option<String> {
    if !exists(dir) {
        return None;
    }
    let s = load(dir);
    let p = s.active_profile();
    if p.id == DEFAULT_ID {
        return None;
    }
    crate::saveimport::uid_key(&p.uid)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(tag: &str, ini: &str) -> std::path::PathBuf {
        static N: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
        let d = std::env::temp_dir().join(format!("pkp-prof-{}-{}-{}", tag, std::process::id(), N.fetch_add(1, std::sync::atomic::Ordering::SeqCst)));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(&d).unwrap();
        if !ini.is_empty() {
            fs::write(d.join(PKP_INI), ini).unwrap();
        }
        d
    }

    fn ini_of(d: &Path, s: &str, k: &str) -> Option<String> {
        Ini::parse(&fs::read_to_string(d.join(PKP_INI)).unwrap_or_default()).get(s, k)
    }

    #[test]
    fn nothing_is_written_until_a_profile_is_added() {
        let d = tmp("none", "[video]\nwidth = 1920\n");
        assert!(apply_active(&d).is_ok());
        assert!(!exists(&d));
        assert_eq!(fs::read_to_string(d.join(PKP_INI)).unwrap(), "[video]\nwidth = 1920\n");
        assert_eq!(load(&d).profiles.len(), 1);
        assert_eq!(strict_save_key(&d), None);
    }

    #[test]
    fn the_default_profile_keeps_todays_uid() {
        let uid = "0123456789abcdef0123456789abcdef";
        let d = tmp("keep", &format!("[account]\nuid = {}\nnickname = Ash\n", uid));
        let s = add(&d, "Misty").unwrap();
        let def = s.get(DEFAULT_ID).unwrap();
        assert_eq!((def.uid.as_str(), def.nickname.as_str(), def.name.as_str()), (uid, "Ash", "Ash"));
        let m = s.profiles.iter().find(|p| p.name == "Misty").unwrap();
        assert_ne!(m.uid, uid);
        assert_ne!(m.uid, BUILT_IN_UID);
        assert_eq!(m.uid.len(), 32);
        assert_eq!(m.nickname, "Misty");
        // the existing ini is untouched by adding
        assert_eq!(ini_of(&d, "account", "uid").as_deref(), Some(uid));
    }

    #[test]
    fn switching_writes_and_switching_back_restores() {
        let d = tmp("switch", "[video]\nwidth = 1920\n");
        let s = add(&d, "Misty").unwrap();
        let misty = s.profiles.iter().find(|p| p.name == "Misty").unwrap().clone();
        select(&d, &misty.id).unwrap();
        assert_eq!(ini_of(&d, "account", "uid"), Some(misty.uid.clone()));
        assert_eq!(ini_of(&d, "account", "nickname").as_deref(), Some("Misty"));
        assert_eq!(ini_of(&d, "net", "uid"), Some(misty.net_uid.clone()));
        assert_eq!(strict_save_key(&d), crate::saveimport::uid_key(&misty.uid));
        select(&d, DEFAULT_ID).unwrap();
        // the default had no uid: the keys are cleared so the runtime's built-in user plays the existing save again
        assert_eq!(ini_of(&d, "account", "uid").as_deref().map(str::trim), Some(""));
        assert_eq!(ini_of(&d, "net", "uid").as_deref().map(str::trim), Some(""));
        assert_eq!(ini_of(&d, "video", "width").as_deref(), Some("1920"));
        assert_eq!(ini_of(&d, "account", "nickname").as_deref(), Some("Player"));
        assert_eq!(strict_save_key(&d), None);
    }

    #[test]
    fn names_are_checked() {
        let d = tmp("names", "");
        assert!(add(&d, "   ").is_err());
        assert!(add(&d, &"x".repeat(25)).is_err());
        assert!(add(&d, "a\nb").is_err());
        add(&d, "Misty").unwrap();
        assert!(add(&d, "misty").is_err());
        assert!(rename(&d, DEFAULT_ID, "MISTY").is_err());
        assert!(rename(&d, DEFAULT_ID, "Brock").is_ok());
    }

    #[test]
    fn remove_keeps_the_first_and_the_save_folder() {
        let d = tmp("remove", "");
        let s = add(&d, "Misty").unwrap();
        let id = s.profiles.iter().find(|p| p.name == "Misty").unwrap().id.clone();
        let folder = d.join("save").join("account").join("x");
        fs::create_dir_all(&folder).unwrap();
        select(&d, &id).unwrap();
        assert!(remove(&d, DEFAULT_ID).is_err());
        let s = remove(&d, &id).unwrap();
        assert_eq!(s.active, DEFAULT_ID);
        assert_eq!(s.profiles.len(), 1);
        assert!(folder.is_dir());
        assert!(remove(&d, "nope").is_err());
    }

    #[test]
    fn nickname_is_cut_on_a_character_boundary() {
        assert!(nickname_of(&"é".repeat(24)).len() <= MAX_NICK_BYTES);
        assert_eq!(nickname_of("Misty"), "Misty");
    }

    #[test]
    fn a_broken_file_falls_back_to_the_default() {
        let d = tmp("broken", "");
        fs::write(d.join(FILE), "{ not json").unwrap();
        assert_eq!(load(&d).profiles.len(), 1);
    }

    fn damaged_copies(d: &Path) -> Vec<std::path::PathBuf> {
        let mut v: Vec<_> = fs::read_dir(d).unwrap().filter_map(|e| e.ok()).map(|e| e.path()).filter(|p| p.file_name().unwrap().to_string_lossy().starts_with("profiles.json.damaged-")).collect();
        v.sort();
        v
    }

    #[test]
    fn a_damaged_profile_file_is_copied_aside_before_it_is_replaced() {
        for (tag, bytes) in [("cut", "{\"active\":\"default\",\"profiles\":[{\"id\":\"def"), ("empty", "{\"active\":\"default\",\"profiles\":[]}"), ("junk", "not json at all")] {
            let d = tmp(tag, "[account]\nuid = aaaa\nnickname = Koi\n");
            fs::write(d.join(FILE), bytes).unwrap();
            add(&d, "Sam").unwrap();
            let copies = damaged_copies(&d);
            assert_eq!(copies.len(), 1, "{}", tag);
            assert_eq!(fs::read_to_string(&copies[0]).unwrap(), bytes, "{}: the copy is the original, byte for byte", tag);
            let now = load(&d);
            assert!(now.profiles.iter().any(|p| p.name == "Sam"), "{}: the new store is written", tag);
            assert!(!is_damaged(&d.join(FILE)), "{}: the new file reads", tag);
        }
    }

    #[test]
    fn a_good_profile_file_leaves_no_copy_and_a_second_damage_gets_its_own_name() {
        let d = tmp("good", "[account]\nuid = aaaa\nnickname = Koi\n");
        add(&d, "Sam").unwrap();
        add(&d, "Alex").unwrap();
        assert!(damaged_copies(&d).is_empty(), "a valid file is never copied");
        fs::write(d.join(FILE), "broken one").unwrap();
        add(&d, "Kim").unwrap();
        fs::write(d.join(FILE), "broken two").unwrap();
        add(&d, "Lee").unwrap();
        let copies = damaged_copies(&d);
        assert_eq!(copies.len(), 2, "two damages, two copies");
        let texts: Vec<String> = copies.iter().map(|p| fs::read_to_string(p).unwrap()).collect();
        assert!(texts.contains(&"broken one".to_string()) && texts.contains(&"broken two".to_string()));
    }

    #[test]
    fn reading_never_writes_a_copy() {
        let d = tmp("read", "[account]\nuid = aaaa\n");
        fs::write(d.join(FILE), "broken").unwrap();
        let _ = load(&d);
        assert!(damaged_copies(&d).is_empty());
        assert_eq!(fs::read_to_string(d.join(FILE)).unwrap(), "broken");
    }

    #[test]
    fn a_save_is_refused_when_the_damaged_file_cannot_be_copied() {
        let d = tmp("refuse", "[account]\nuid = aaaa\n");
        fs::write(d.join(FILE), "broken").unwrap();
        // every candidate name is taken, so no copy can be made. The stamp has a resolution of one second, so start at the top of a second
        // and take the names of this second and the next: the 1000 writes and add() may straddle the change.
        let ms = chrono::Utc::now().timestamp_subsec_millis() as u64;
        std::thread::sleep(std::time::Duration::from_millis(1000 - ms));
        let now = chrono::Utc::now();
        for stamp in [now, now + chrono::Duration::seconds(1)].map(|t| t.format("%Y%m%d-%H%M%S").to_string()) {
            for i in 0..1000 {
                let name = if i == 0 { format!("{}.damaged-{}", FILE, stamp) } else { format!("{}.damaged-{}-{}", FILE, stamp, i + 1) };
                fs::write(d.join(name), "x").unwrap();
            }
        }
        let err = add(&d, "Sam").unwrap_err();
        assert!(err.contains("damaged"), "{}", err);
        assert_eq!(fs::read_to_string(d.join(FILE)).unwrap(), "broken", "the damaged file is untouched");
    }

    fn write_save(d: &Path, uid: &str, tag: &[u8]) {
        let key = crate::saveimport::account_key_of(uid);
        let root = d.join("save").join("account").join(key);
        fs::create_dir_all(root.join("G00")).unwrap();
        fs::create_dir_all(root.join("S00")).unwrap();
        fs::write(root.join("G00").join("CMNDAT.BIN"), tag).unwrap();
        fs::write(root.join("S00").join("SYDAT.BIN"), tag).unwrap();
    }

    #[test]
    fn coop_profile_has_own_folder_members_version_and_never_touches_pkp_ini() {
        let sp_uid = "0123456789abcdef0123456789abcdef";
        let d = tmp("coop", &format!("[account]\nuid = {}\nnickname = Ash\n[fs]\nsave_dir = save\n", sp_uid));
        let s = add_coop(&d, "Group One", None, None).unwrap();
        let cp = s.profiles.iter().find(|p| p.name == "Group One").unwrap().clone();
        let coop = cp.coop.as_ref().expect("coop metadata");
        assert_ne!(cp.uid, sp_uid);
        assert_eq!(coop.room_code.len(), 19);
        assert_eq!(coop.world_version, 0);
        assert_eq!(coop.members.len(), 1);
        assert_eq!(coop.members[0].name, "Group One");
        assert_eq!(coop.members[0].role, "host");

        // selecting a co-op profile never writes its uid or co-op settings into pkp.ini
        select(&d, &cp.id).unwrap();
        assert_eq!(ini_of(&d, "account", "uid").as_deref(), Some(sp_uid));
        assert_eq!(ini_of(&d, "account", "nickname").as_deref(), Some("Ash"));
        assert_eq!(strict_save_key(&d), crate::saveimport::uid_key(&cp.uid));

        // members, room code, relay, world version updates
        let s = update_coop(
            &d,
            &cp.id,
            Some("abcd 1234 efgh 5678"),
            Some("203.0.113.5:47610"),
            Some(vec![
                Member { name: "Group One".into(), net_uid: cp.net_uid.clone(), role: "host".into() },
                Member { name: "Misty".into(), net_uid: String::new(), role: "visitor".into() },
            ]),
        )
        .unwrap();
        let updated = s.get(&cp.id).unwrap().coop.as_ref().unwrap();
        assert_eq!(updated.room_code, "ABCD-1234-EFGH-5678");
        assert_eq!(updated.relay, "203.0.113.5:47610");
        assert_eq!(updated.members.len(), 2);

        let s = bump_world_version(&d, &cp.id).unwrap();
        assert_eq!(s.get(&cp.id).unwrap().coop.as_ref().unwrap().world_version, 1);
    }

    #[test]
    fn copy_between_singleplayer_and_coop_modes_uses_copy_account() {
        let sp_uid = "0123456789abcdef0123456789abcdef";
        let d = tmp("copy-modes", &format!("[account]\nuid = {}\nnickname = Ash\n[fs]\nsave_dir = save\n", sp_uid));
        write_save(&d, sp_uid, b"singleplayer-world");

        let s = start_coop_from(&d, DEFAULT_ID, "Island Co-op", None, None).unwrap();
        let cp = s.profiles.iter().find(|p| p.name == "Island Co-op").unwrap().clone();
        let cp_coop = cp.coop.as_ref().unwrap();
        assert_eq!(cp_coop.world_version, 1);
        let cp_key = crate::saveimport::uid_key(&cp.uid).unwrap();
        assert_eq!(fs::read(d.join("save").join("account").join(&cp_key).join("G00").join("CMNDAT.BIN")).unwrap(), b"singleplayer-world");

        // cannot start co-op from a co-op profile, nor copy singleplayer to singleplayer
        assert!(start_coop_from(&d, &cp.id, "Bad", None, None).is_err());
        assert!(copy_to_singleplayer(&d, DEFAULT_ID, "Bad2").is_err());

        // modify co-op save and copy back to a new singleplayer profile
        fs::write(d.join("save").join("account").join(&cp_key).join("G00").join("CMNDAT.BIN"), b"coop-progress").unwrap();
        let s = copy_to_singleplayer(&d, &cp.id, "Solo Copy").unwrap();
        let sp2 = s.profiles.iter().find(|p| p.name == "Solo Copy").unwrap();
        assert!(sp2.coop.is_none());
        let sp2_key = crate::saveimport::uid_key(&sp2.uid).unwrap();
        assert_eq!(fs::read(d.join("save").join("account").join(&sp2_key).join("G00").join("CMNDAT.BIN")).unwrap(), b"coop-progress");
        // original singleplayer save was untouched
        let sp_key = crate::saveimport::uid_key(sp_uid).unwrap();
        assert_eq!(fs::read(d.join("save").join("account").join(&sp_key).join("G00").join("CMNDAT.BIN")).unwrap(), b"singleplayer-world");
    }

    #[test]
    fn old_profiles_json_without_coop_field_loads_as_singleplayer() {
        let d = tmp("compat", "");
        fs::write(
            d.join(FILE),
            r#"{"active":"default","profiles":[{"id":"default","name":"Player","uid":"","nickname":"Player","net_uid":""}]}"#,
        )
        .unwrap();
        let s = load(&d);
        assert_eq!(s.profiles.len(), 1);
        assert!(s.profiles[0].coop.is_none());
    }
}
