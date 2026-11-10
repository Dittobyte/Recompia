// SPDX-License-Identifier: GPL-3.0-or-later
//! Link Play checks the launcher page asks for: can this PC reach a host's port, what the Linux firewall looks like, and which of the
//! user's Tailscale devices are hosting. Read only: nothing here opens a port, changes a firewall or network setting, or sends anything but
//! a TCP connection attempt.
//!
//! THE CONTRACT. The types below are what `api.js` (linkProbe, setupFacts, tailscalePeers) hands the page and what `src/dev/mock.js`
//! imitates. The functions are stubs that answer with the "nothing found" shape until the Rust mission (LP-PNP-02) fills them in; the
//! page mission (LP-PNP-03) builds on the shapes only. Change a shape only with the lead's word: both missions depend on it.

use serde::Serialize;

/// The coop host port a probe tries when the address has none.
#[allow(dead_code)] // the stub does not use it yet: LP-PNP-02 does and removes this attribute
pub const HOST_PORT: u16 = 47600;

/// `link_probe`: one TCP connection attempt to `addr` (IPv4, optional `:port`, default 47600). It sends nothing and closes at once.
///   answered     the port accepted the connection (a host, or at least something listening), after `ms`
///   refused      the machine answered and nothing listens there (not hosting yet, or a firewall that rejects)
///   timed_out    no answer within the timeout (blocked by a firewall that drops, a different network, or the machine is off)
///   bad_address  not an IPv4 address with an optional port; `message` says what is wrong, in words
/// JSON: `{ "kind": "answered", "ms": 12 }` | `{ "kind": "refused" }` | `{ "kind": "timed_out" }` | `{ "kind": "bad_address", "message": "..." }`
#[allow(dead_code)] // the stub only builds BadAddress: LP-PNP-02 builds the rest and removes this attribute
#[derive(Serialize, Clone, Debug, PartialEq)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ProbeResult {
    Answered { ms: u64 },
    Refused,
    TimedOut,
    BadAddress { message: String },
}

/// `setup_facts`: what the firewall says, read without privileges. `state` is "running", "not_running" or "absent" (no firewalld here:
/// Windows, SteamOS, a distribution without it). `zones`: the active zone names. `ports`: the ports opened in them, as firewalld prints
/// them ("47600/tcp", "1025-65535/udp"). The page turns facts and `lan_report` into plain findings; this only reports.
/// JSON: `{ "firewalld": { "state": "running", "zones": ["FedoraWorkstation"], "ports": ["1025-65535/tcp", "1025-65535/udp"] } }`
#[derive(Serialize, Clone, Debug, PartialEq)]
pub struct SetupFacts {
    pub firewalld: Firewalld,
}

#[derive(Serialize, Clone, Debug, PartialEq)]
pub struct Firewalld {
    pub state: String,
    pub zones: Vec<String>,
    pub ports: Vec<String>,
}

/// `tailscale_peers`: the user's tailnet from `tailscale status --json`. `found` false: no tailscale program or no answer in time,
/// with `error` in words ("Tailscale is not installed", "Tailscale is not running", ...). `self` is this device's Tailscale addresses.
/// Each peer: `name` (short host name), `dns_name` (MagicDNS, may be empty), `ips` (its 100.x addresses), `online`, and `hosting`: true when
/// its port 47600 accepted a connection, false when it did not, null for a peer that is offline or was not probed.
/// JSON: `{ "found": true, "self": ["100.64.0.5"], "peers": [{ "name": "deck", "dns_name": "deck.tail1234.ts.net", "ips": ["100.64.0.9"], "online": true, "hosting": true }] }`
#[derive(Serialize, Clone, Debug, PartialEq)]
pub struct TailscaleReport {
    pub found: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    #[serde(rename = "self")]
    pub self_ips: Vec<String>,
    pub peers: Vec<Peer>,
}

#[derive(Serialize, Clone, Debug, PartialEq)]
pub struct Peer {
    pub name: String,
    pub dns_name: String,
    pub ips: Vec<String>,
    pub online: bool,
    pub hosting: Option<bool>,
}

// ---- stubs: LP-PNP-02 replaces the bodies, keeps the signatures ----

/// Tries one TCP connection to `addr`.
pub fn probe(_addr: &str) -> ProbeResult {
    ProbeResult::BadAddress { message: "The connection test is not built yet.".into() }
}

/// Reads the firewall state.
pub fn setup_facts() -> SetupFacts {
    SetupFacts { firewalld: Firewalld { state: "absent".into(), zones: vec![], ports: vec![] } }
}

/// Reads the tailnet and probes the online peers.
pub fn tailscale_peers() -> TailscaleReport {
    TailscaleReport { found: false, error: Some("The Tailscale finder is not built yet.".into()), self_ips: vec![], peers: vec![] }
}
