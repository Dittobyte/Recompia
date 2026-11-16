// SPDX-License-Identifier: GPL-3.0-or-later
//! Play and the game's state.

use std::path::PathBuf;
use tauri::AppHandle;

use crate::config::{load_config, Config};
use crate::guard::{ensure_idle, Need};
use crate::paths::game_dir_of;
use crate::readiness::readiness;
use crate::states::{PlayArg, States};
use crate::{coop, play, profiles, relay_cmds, settings};

fn ready_to_play(app: &AppHandle) -> Result<(PathBuf, Config), String> {
    ensure_idle(app, &[Need::Swap, Need::Storage])?;
    let cfg = load_config(app);
    let probe = readiness(&cfg);
    if probe.readiness.kind != "ready" {
        return Err(probe.readiness.detail);
    }
    let dir = game_dir_of(&cfg);
    // macOS: Set up wrote pkp.ini from the player's dump and there is no dump choice to apply here, so readiness carries no
    // paths. Everywhere else they name the exefs and romfs to write.
    if let Some(paths) = probe.paths {
        settings::ensure_play_paths(&dir, &paths.exefs, &paths.romfs, &paths.romfs_base, &paths.aoc_dir)?;
    }
    Ok((dir, cfg))
}

fn ensure_normal_profile(profile: &profiles::Profile) -> Result<(), String> {
    if profile.coop.is_some() {
        return Err("This profile is a co-op save. Start it from Co-op instead, or switch to a singleplayer profile.".into());
    }
    Ok(())
}

fn prepare_coop_launch(store: &profiles::Store, id: Option<&str>) -> Result<coop::Launch, String> {
    let profile = match id {
        Some(pid) => store.get(pid).ok_or("That profile does not exist.")?,
        None => store.active_profile(),
    };
    coop::Launch::prepare(profile)
}

/// `without_mods`: this one start runs without mods (the runtime's [mods] off, passed in the game's environment only).
#[tauri::command]
pub fn play(app: AppHandle, without_mods: Option<bool>) -> Result<(), String> {
    // an update may be preparing: Play waits only for its swap, and holds it off until this game has started (play::start)
    let updates = app.updates();
    let _swap = updates.hold_swap();
    let (dir, cfg) = ready_to_play(&app)?;
    let store = profiles::load(&dir);
    ensure_normal_profile(store.active_profile())?;
    profiles::apply_active(&dir)?;
    play::start(&app, &dir, &cfg, without_mods.unwrap_or(false), None)
}

/// Launches a co-op profile with its co-op settings in the process environment only (nothing written to pkp.ini).
#[tauri::command]
pub fn play_coop(app: AppHandle, id: Option<String>, without_mods: Option<bool>) -> Result<(), String> {
    let updates = app.updates();
    let _swap = updates.hold_swap();
    let (dir, cfg) = ready_to_play(&app)?;
    let store = profiles::load(&dir);
    let launch = prepare_coop_launch(&store, id.as_deref())?;
    if coop::is_loopback_relay(&launch.relay) && !relay_cmds::is_running(&app) {
        return Err("The relay on this PC is off. Turn on Run a relay on this PC on the Link Play page, or clear the relay address to use the public relay.".into());
    }
    play::start(&app, &dir, &cfg, without_mods.unwrap_or(false), Some(launch))
}

#[tauri::command]
pub fn game_status(state: PlayArg) -> play::Status {
    state.0.lock().unwrap().clone()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn profile(id: &str, coop: Option<profiles::Coop>) -> profiles::Profile {
        profiles::Profile {
            id: id.into(),
            name: id.into(),
            uid: "0123456789abcdef0123456789abcdef".into(),
            nickname: id.into(),
            net_uid: "fedcba9876543210fedcba9876543210".into(),
            coop,
        }
    }

    #[test]
    fn normal_launch_refuses_coop_save_and_coop_launch_refuses_singleplayer_save() {
        let sp = profile("sp", None);
        let cp = profile(
            "cp",
            Some(profiles::Coop {
                room_code: "ABCD-EFGH-JKMN-PQRS".into(),
                relay: String::new(),
                members: vec![],
                world_version: 1,
            }),
        );
        let store = profiles::Store { active: "cp".into(), profiles: vec![sp.clone(), cp.clone()] };

        assert!(ensure_normal_profile(&sp).is_ok());
        assert!(ensure_normal_profile(&cp).unwrap_err().contains("co-op save"));
        assert!(prepare_coop_launch(&store, Some("sp")).unwrap_err().contains("singleplayer save"));
        assert!(prepare_coop_launch(&store, Some("cp")).is_ok());
        assert!(prepare_coop_launch(&store, None).is_ok());
    }
}
