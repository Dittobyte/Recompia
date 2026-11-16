// SPDX-License-Identifier: GPL-3.0-or-later
//! Profile commands (profiles.rs holds the logic, play_time.rs the play time).

use std::collections::HashMap;
use std::path::PathBuf;
use tauri::AppHandle;

use crate::config::game_dir;
use crate::guard::{ensure_idle, Need};
use crate::{play_time, profiles};

/// The profiles (names and ids) and which one plays.
#[tauri::command]
pub fn profiles_list(app: AppHandle) -> profiles::Store {
    profiles::load(&game_dir(&app))
}

/// Seconds played by each profile, by id, as the game counts them; a profile without a time yet is left out.
#[tauri::command]
pub fn profiles_play_time(app: AppHandle) -> HashMap<String, u64> {
    play_time::of_profiles(&game_dir(&app))
}

pub fn profiles_guard(app: &AppHandle) -> Result<PathBuf, String> {
    ensure_idle(app, &[Need::Game("The game is running. Close it first, then change profiles."), Need::Setup("Setup is running. Wait for it to finish, then change your mods."), Need::Update])?;
    Ok(game_dir(app))
}

#[tauri::command]
pub fn profile_add(app: AppHandle, name: String) -> Result<profiles::Store, String> {
    profiles::add(&profiles_guard(&app)?, &name)
}

#[tauri::command]
pub fn profile_rename(app: AppHandle, id: String, name: String) -> Result<profiles::Store, String> {
    let dir = profiles_guard(&app)?;
    let s = profiles::rename(&dir, &id, &name)?;
    profiles::apply_active(&dir)?;
    Ok(s)
}

#[tauri::command]
pub fn profile_remove(app: AppHandle, id: String) -> Result<profiles::Store, String> {
    let dir = profiles_guard(&app)?;
    let profiles = profiles::remove(&dir, &id)?;
    crate::mods::refresh_plan(&dir)?;
    Ok(profiles)
}

#[tauri::command]
pub fn profile_select(app: AppHandle, id: String) -> Result<profiles::Store, String> {
    let dir = profiles_guard(&app)?;
    let profiles = profiles::select(&dir, &id)?;
    crate::mods::refresh_plan(&dir)?;
    Ok(profiles)
}


#[tauri::command]
pub fn profile_add_coop(app: AppHandle, name: String, room_code: Option<String>, relay: Option<String>) -> Result<profiles::Store, String> {
    profiles::add_coop(&profiles_guard(&app)?, &name, room_code.as_deref(), relay.as_deref())
}

#[tauri::command]
pub fn profile_start_coop_from(app: AppHandle, source_id: String, name: String, room_code: Option<String>, relay: Option<String>) -> Result<profiles::Store, String> {
    profiles::start_coop_from(&profiles_guard(&app)?, &source_id, &name, room_code.as_deref(), relay.as_deref())
}

#[tauri::command]
pub fn profile_copy_to_singleplayer(app: AppHandle, source_id: String, name: String) -> Result<profiles::Store, String> {
    profiles::copy_to_singleplayer(&profiles_guard(&app)?, &source_id, &name)
}

#[tauri::command]
pub fn profile_coop_update(
    app: AppHandle,
    id: String,
    room_code: Option<String>,
    relay: Option<String>,
    members: Option<Vec<profiles::Member>>,
) -> Result<profiles::Store, String> {
    profiles::update_coop(&profiles_guard(&app)?, &id, room_code.as_deref(), relay.as_deref(), members)
}
