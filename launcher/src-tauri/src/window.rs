// SPDX-License-Identifier: GPL-3.0-or-later
//! Where the window opens and where it was left.

use tauri::{Manager, WindowEvent};

use crate::config::{load_config, save_config, WindowPlace};
use crate::states::States;

/// A handheld's logical screen is smaller than the default window: there the window starts maximized, which also keeps it clear of the
/// taskbar. On a larger screen the window opens where the player left it (`saved`), else at the default size from tauri.conf.json.
pub fn fit_window(window: &tauri::WebviewWindow, saved: Option<WindowPlace>) -> tauri::Result<()> {
    // Steam's game mode (Gamescope) shows one window on the whole screen, whatever its size: start maximized there
    if in_game_mode() {
        return window.maximize();
    }
    let Some(monitor) = window.current_monitor()? else { return Ok(()) };
    let scale = monitor.scale_factor();
    let screen = monitor.size().to_logical::<f64>(scale);
    if screen.width <= 1400.0 || screen.height < 800.0 {
        return window.maximize();
    }
    // the size the player left it at, if it still fits this screen (another monitor may be gone since)
    if let Some(p) = saved.filter(|p| p.width >= 900.0 && p.height >= 440.0 && p.width <= screen.width && p.height <= screen.height) {
        window.set_size(tauri::Size::Logical(tauri::LogicalSize::new(p.width, p.height)))?;
        // placed only where a monitor still is; Wayland ignores the position and the compositor places it
        let on_screen = window.available_monitors()?.iter().any(|m| {
            let (o, sz) = (m.position().to_logical::<f64>(m.scale_factor()), m.size().to_logical::<f64>(m.scale_factor()));
            p.x + 40.0 >= o.x && p.y + 20.0 >= o.y && p.x + 40.0 <= o.x + sz.width && p.y + 20.0 <= o.y + sz.height
        });
        if on_screen {
            window.set_position(tauri::Position::Logical(tauri::LogicalPosition::new(p.x, p.y)))?;
        }
    }
    Ok(())
}

/// True when the launcher runs in Steam's game mode (Steam Deck, or another handheld on SteamOS or Bazzite in Gamescope).
pub fn in_game_mode() -> bool {
    game_mode_from(|name| std::env::var(name).ok())
}

/// `get` reads one environment variable. Game mode is: `SteamDeck=1`, `SteamGamepadUI` set, `GAMESCOPE_WAYLAND_DISPLAY` set, or a desktop name containing gamescope.
fn game_mode_from(get: impl Fn(&str) -> Option<String>) -> bool {
    let set = |name: &str| get(name).is_some_and(|v| !v.is_empty());
    get("SteamDeck").is_some_and(|v| v == "1")
        || set("SteamGamepadUI")
        || set("GAMESCOPE_WAYLAND_DISPLAY")
        || get("XDG_CURRENT_DESKTOP").is_some_and(|v| v.to_ascii_lowercase().contains("gamescope"))
}

/// After each page load in game mode: the page gets `data-gamemode` on <html>, which turns on the larger controls (gamemode.css).
pub fn mark_game_mode(webview: &tauri::Webview, payload: &tauri::webview::PageLoadPayload<'_>) {
    if in_game_mode() && matches!(payload.event(), tauri::webview::PageLoadEvent::Finished) {
        let _ = webview.eval("document.documentElement.dataset.gamemode = '1'");
    }
}

/// The window's size and place as it closes, for the next start (fit_window). A maximized window keeps what was saved before.
pub fn place_of(window: &tauri::Window) -> Option<WindowPlace> {
    if window.is_maximized().unwrap_or(true) || window.is_minimized().unwrap_or(true) {
        return None;
    }
    let scale = window.scale_factor().ok()?;
    let size = window.inner_size().ok()?.to_logical::<f64>(scale);
    let pos = window.outer_position().ok()?.to_logical::<f64>(scale);
    // a closing window can report a degenerate size: never let it replace a good saved place
    (size.width >= 900.0 && size.height >= 440.0).then_some(WindowPlace { x: pos.x, y: pos.y, width: size.width, height: size.height })
}

/// Close handling: an update that is moving files keeps the window open; otherwise the window's place is saved for the next start.
pub fn on_event(window: &tauri::Window, event: &WindowEvent) {
    if let tauri::WindowEvent::CloseRequested { api, .. } = event {
        if window.updates().applying() {
            api.prevent_close();
            return;
        }
        if let Some(place) = place_of(window) {
            let app = window.app_handle();
            let mut cfg = load_config(app);
            if cfg.window != Some(place) {
                cfg.window = Some(place);
                let _ = save_config(app.clone(), cfg);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::game_mode_from;

    fn mode(vars: &[(&str, &str)]) -> bool {
        game_mode_from(|name| vars.iter().find(|(k, _)| *k == name).map(|(_, v)| v.to_string()))
    }

    #[test]
    fn plain_desktop_is_not_game_mode() {
        assert!(!mode(&[]));
        assert!(!mode(&[("XDG_CURRENT_DESKTOP", "KDE")]));
        assert!(!mode(&[("SteamDeck", "0"), ("SteamGamepadUI", ""), ("GAMESCOPE_WAYLAND_DISPLAY", "")]));
    }

    #[test]
    fn each_steam_signal_means_game_mode() {
        assert!(mode(&[("SteamDeck", "1")]));
        assert!(mode(&[("SteamGamepadUI", "1")]));
        assert!(mode(&[("GAMESCOPE_WAYLAND_DISPLAY", "gamescope-0")]));
        assert!(mode(&[("XDG_CURRENT_DESKTOP", "gamescope")]));
        assert!(mode(&[("XDG_CURRENT_DESKTOP", "X-Gamescope:GNOME")]));
    }
}
