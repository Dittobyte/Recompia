// SPDX-License-Identifier: GPL-3.0-or-later
//! Thin commands over linkcheck.rs for the Link Play page. Each runs the (blocking) check on a worker thread.

use crate::linkcheck::{self, ProbeResult, SetupFacts, TailscaleReport};

/// One TCP connection attempt to a host's address: answered, refused, timed out or a bad address (never sends data).
#[tauri::command]
pub async fn link_probe(addr: String) -> ProbeResult {
    tauri::async_runtime::spawn_blocking(move || linkcheck::probe(&addr)).await.unwrap_or(ProbeResult::TimedOut)
}

/// What the Linux firewall looks like (read only; "absent" where there is no firewalld).
#[tauri::command]
pub async fn setup_facts() -> SetupFacts {
    tauri::async_runtime::spawn_blocking(linkcheck::setup_facts).await.unwrap_or_else(|_| linkcheck::setup_facts())
}

/// The user's Tailscale devices and which of them are hosting.
#[tauri::command]
pub async fn tailscale_peers() -> TailscaleReport {
    tauri::async_runtime::spawn_blocking(linkcheck::tailscale_peers).await.unwrap_or_else(|_| linkcheck::tailscale_peers())
}
