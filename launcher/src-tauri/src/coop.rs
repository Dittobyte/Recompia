// SPDX-License-Identifier: GPL-3.0-or-later
//! Seamless co-op group rules, private room codes and per-launch environment (#701, step 1).
//!
//! A co-op profile keeps its save in its own account folder (profiles.rs) and never writes co-op settings or its account uid into
//! pkp.ini: each co-op launch receives its identity, relay, private room code and co-op flags through the process environment for that
//! launch only. On the relay, the group's room uses a room code (`XXXX-XXXX-XXXX-XXXX`, Crockford base32 matching pokopia-coop's
//! coop_auth.c), so it never enters the public room list.

use std::process::Command;

use crate::profiles::{clean_name, Member, Profile};
use crate::saveimport;

/// The project's public relay (same address as src/linkplay-model.js PUBLIC_RELAY).
pub const PUBLIC_RELAY: &str = "204.216.111.91:47610";
const B32: &[u8; 32] = b"0123456789ABCDEFGHJKMNPQRSTVWXYZ";
const ROOM_LEN: usize = 16;

/// Generates a fresh 80-bit private room code formatted as `XXXX-XXXX-XXXX-XXXX`.
pub fn new_room_code() -> Result<String, String> {
    let mut bytes = [0u8; ROOM_LEN];
    pokopia_relay::random::os_random(&mut bytes).map_err(|e| format!("Cannot make a room code: {}", e))?;
    let chars: Vec<char> = bytes.iter().map(|b| B32[(b & 31) as usize] as char).collect();
    Ok(format_room(&chars))
}

fn format_room(chars: &[char]) -> String {
    chars.chunks(4).map(|c| c.iter().collect::<String>()).collect::<Vec<_>>().join("-")
}

/// Normalizes a Crockford base32 room code (case-insensitive, `-` and spaces ignored, O->0, I/L->1) into `XXXX-XXXX-XXXX-XXXX`.
pub fn normalize_room_code(raw: &str) -> Result<String, String> {
    let mut out = Vec::with_capacity(ROOM_LEN);
    for ch in raw.chars() {
        if ch == '-' || ch == ' ' {
            continue;
        }
        let mut up = ch.to_ascii_uppercase();
        if up == 'O' {
            up = '0';
        } else if up == 'I' || up == 'L' {
            up = '1';
        }
        if !up.is_ascii() || !B32.contains(&(up as u8)) || out.len() == ROOM_LEN {
            return Err("Type a 16-character room code (XXXX-XXXX-XXXX-XXXX).".into());
        }
        out.push(up);
    }
    if out.len() != ROOM_LEN {
        return Err("Type a 16-character room code (XXXX-XXXX-XXXX-XXXX).".into());
    }
    Ok(format_room(&out))
}

/// Validates an optional relay address (`<IPv4>[:<port>]`); empty means the public relay.
/// Refuses 0.x, multicast and up, and port 0 (matching coop_cfg_parse_ex / linkplay-model.js parseAddr).
pub fn validate_relay(raw: &str) -> Result<String, String> {
    let s = raw.trim();
    if s.is_empty() {
        return Ok(String::new());
    }
    let (host, port) = match s.split_once(':') {
        Some((h, p)) => (h, Some(p)),
        None => (s, None),
    };
    let octets: Vec<&str> = host.split('.').collect();
    if octets.len() != 4 {
        return Err("Type a relay address like 203.0.113.5:47610, or leave it empty for the public relay.".into());
    }
    let nums: Option<Vec<u8>> = octets.iter().map(|o| o.parse::<u8>().ok()).collect();
    let Some(nums) = nums else {
        return Err("Type a relay address like 203.0.113.5:47610, or leave it empty for the public relay.".into());
    };
    if nums[0] == 0 || nums[0] >= 224 {
        return Err("Type a relay address like 203.0.113.5:47610, or leave it empty for the public relay.".into());
    }
    if let Some(p) = port {
        let Ok(n) = p.parse::<u16>() else {
            return Err("Type a relay address like 203.0.113.5:47610, or leave it empty for the public relay.".into());
        };
        if n == 0 {
            return Err("Type a relay address like 203.0.113.5:47610, or leave it empty for the public relay.".into());
        }
    }
    Ok(s.to_string())
}

pub fn effective_relay(relay: &str) -> &str {
    let r = relay.trim();
    if r.is_empty() { PUBLIC_RELAY } else { r }
}

pub fn is_loopback_relay(relay: &str) -> bool {
    relay.trim().starts_with("127.")
}

fn is_hex32(s: &str) -> bool {
    s.len() == 32 && s.chars().all(|c| c.is_ascii_hexdigit())
}

/// Validates and normalizes recorded group members.
pub fn validate_members(members: Vec<Member>) -> Result<Vec<Member>, String> {
    let mut out: Vec<Member> = Vec::with_capacity(members.len());
    for m in members {
        let name = clean_name(&m.name)?;
        let net_uid = m.net_uid.trim().to_ascii_lowercase();
        if !net_uid.is_empty() && !is_hex32(&net_uid) {
            return Err("A member's network ID must be 32 hex digits.".into());
        }
        let role = match m.role.trim().to_ascii_lowercase().as_str() {
            "" => String::new(),
            "host" => "host".into(),
            "visitor" => "visitor".into(),
            _ => return Err("A member's role must be host or visitor.".into()),
        };
        if out.iter().any(|x| x.name.eq_ignore_ascii_case(&name) && (!net_uid.is_empty() && x.net_uid == net_uid || net_uid.is_empty() && x.net_uid.is_empty())) {
            return Err(format!("{} is already in the group.", name));
        }
        out.push(Member { name, net_uid, role });
    }
    Ok(out)
}

fn env_nickname(p: &Profile) -> String {
    if p.nickname.is_empty() || (cfg!(windows) && !p.nickname.is_ascii()) {
        "Player".into()
    } else {
        p.nickname.clone()
    }
}

/// Everything a co-op launch hands to the game process and watchers without touching pkp.ini.
#[derive(Clone, Debug)]
pub struct Launch {
    pub profile_id: String,
    pub profile_name: String,
    pub account_uid: String,
    pub save_key: String,
    pub relay: String,
    env: Vec<(&'static str, String)>,
}

impl Launch {
    pub fn prepare(profile: &Profile) -> Result<Self, String> {
        let coop = profile
            .coop
            .as_ref()
            .ok_or_else(|| "This profile is a singleplayer save. Pick a co-op profile, or start co-op from a copy of it.".to_string())?;
        let save_key = saveimport::uid_key(&profile.uid).ok_or_else(|| "This co-op profile has no valid account ID.".to_string())?;
        let room_code = normalize_room_code(&coop.room_code)?;
        let relay = effective_relay(&validate_relay(&coop.relay)?).to_string();
        let env = vec![
            ("NXRT_ACCOUNT_UID", profile.uid.clone()),
            ("NXRT_ACCOUNT_NICKNAME", env_nickname(profile)),
            ("NXRT_NET_UID", profile.net_uid.clone()),
            ("NXRT_NET_PROTOCOL", "native".into()),
            ("NXRT_NET_MODE", "auto".into()),
            ("NXRT_NET_INTERNET", "true".into()),
            ("NXRT_NET_RELAY", relay.clone()),
            // Private room on the relay (never listed on the public room list once runtime reads room_code under protocol=native)
            ("NXRT_NET_ROOM_CODE", room_code),
            // Co-op mode overrides in the environment only (pkp.ini is left untouched)
            ("NXRT_PKP_COOP", "1".into()),
            ("NXRT_PKP_COOP_WORLD_VERSION", coop.world_version.to_string()),
            ("NXRT_PKP_LINK_INTERACT_EVERYWHERE", "1".into()),
            ("NXRT_PKP_LINK_STORY_COOP", "0".into()),
        ];
        Ok(Self {
            profile_id: profile.id.clone(),
            profile_name: profile.name.clone(),
            account_uid: profile.uid.clone(),
            save_key,
            relay,
            env,
        })
    }

    pub fn apply(&self, cmd: &mut Command) {
        for (k, v) in &self.env {
            cmd.env(k, v);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::profiles::Coop;

    fn env_of<'a>(l: &'a Launch, key: &str) -> Option<&'a str> {
        l.env.iter().find(|(k, _)| *k == key).map(|(_, v)| v.as_str())
    }

    #[test]
    fn room_codes_generate_and_normalize_crockford() {
        let code = new_room_code().unwrap();
        assert_eq!(code.len(), 19);
        assert_eq!(normalize_room_code(&code).unwrap(), code);
        assert_eq!(normalize_room_code("abcd ol12 efgh 3456").unwrap(), "ABCD-0112-EFGH-3456");
        assert!(normalize_room_code("short").is_err());
        assert!(normalize_room_code("ABCD-EFGH-JKLM-NPQU").is_err(), "U is not in Crockford base32");
    }

    #[test]
    fn relay_validation_accepts_empty_or_valid_ipv4() {
        assert_eq!(validate_relay("  ").unwrap(), "");
        assert_eq!(validate_relay("127.0.0.1:47610").unwrap(), "127.0.0.1:47610");
        assert_eq!(validate_relay("203.0.113.5").unwrap(), "203.0.113.5");
        assert!(validate_relay("0.0.0.0:47610").is_err());
        assert!(validate_relay("224.0.0.1:47610").is_err());
        assert!(validate_relay("127.0.0.1:0").is_err());
        assert!(validate_relay("example.com:47610").is_err());
    }

    #[test]
    fn prepare_refuses_singleplayer_profile_and_builds_env_for_coop() {
        let sp = Profile {
            id: "p1".into(),
            name: "Ash".into(),
            uid: "0123456789abcdef0123456789abcdef".into(),
            nickname: "Ash".into(),
            net_uid: "fedcba9876543210fedcba9876543210".into(),
            coop: None,
        };
        assert!(Launch::prepare(&sp).unwrap_err().contains("singleplayer"));

        let cp = Profile {
            coop: Some(Coop {
                room_code: "abcd-efgh-jkmn-pqrs".into(),
                relay: String::new(),
                members: vec![Member { name: "Ash".into(), net_uid: sp.net_uid.clone(), role: "host".into() }],
                world_version: 3,
            }),
            ..sp
        };
        let launch = Launch::prepare(&cp).unwrap();
        assert_eq!(env_of(&launch, "NXRT_ACCOUNT_UID"), Some("0123456789abcdef0123456789abcdef"));
        assert_eq!(env_of(&launch, "NXRT_NET_PROTOCOL"), Some("native"));
        assert_eq!(env_of(&launch, "NXRT_NET_MODE"), Some("auto"));
        assert_eq!(env_of(&launch, "NXRT_NET_INTERNET"), Some("true"));
        assert_eq!(env_of(&launch, "NXRT_NET_RELAY"), Some(PUBLIC_RELAY));
        assert_eq!(env_of(&launch, "NXRT_NET_ROOM_CODE"), Some("ABCD-EFGH-JKMN-PQRS"));
        assert_eq!(env_of(&launch, "NXRT_PKP_COOP"), Some("1"));
        assert_eq!(env_of(&launch, "NXRT_PKP_COOP_WORLD_VERSION"), Some("3"));
        assert_eq!(env_of(&launch, "NXRT_PKP_LINK_INTERACT_EVERYWHERE"), Some("1"));
        assert_eq!(env_of(&launch, "NXRT_PKP_LINK_STORY_COOP"), Some("0"));
    }
}
