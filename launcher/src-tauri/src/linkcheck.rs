// SPDX-License-Identifier: GPL-3.0-or-later
//! Link Play checks the launcher page asks for: can this PC reach a host's port, what the Linux firewall looks like, and which of the
//! user's Tailscale devices are hosting. Read only: nothing here opens a port, changes a firewall or network setting, or sends anything but
//! a TCP connection attempt.
//!
//! THE CONTRACT. The types below are what `api.js` (linkProbe, setupFacts, tailscalePeers) hands the page and what `src/dev/mock.js`
//! imitates. The functions are stubs that answer with the "nothing found" shape until the Rust mission (LP-PNP-02) fills them in; the
//! page mission (LP-PNP-03) builds on the shapes only. Change a shape only with the lead's word: both missions depend on it.

use serde::Serialize;
use std::io::{self, Read};
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, TcpStream};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

/// The coop host port a probe tries when the address has none.
pub const HOST_PORT: u16 = 47600;

/// `link_probe`: one TCP connection attempt to `addr` (IPv4, optional `:port`, default 47600). It sends nothing and closes at once.
///   answered     the port accepted the connection (a host, or at least something listening), after `ms`
///   refused      the machine answered and nothing listens there (not hosting yet, or a firewall that rejects)
///   timed_out    no answer within the timeout (blocked by a firewall that drops, a different network, or the machine is off)
///   bad_address  not an IPv4 address with an optional port; `message` says what is wrong, in words
/// JSON: `{ "kind": "answered", "ms": 12 }` | `{ "kind": "refused" }` | `{ "kind": "timed_out" }` | `{ "kind": "bad_address", "message": "..." }`
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

// ---- implementation (LP-PNP-02) ----

/// How long a probe waits for the TCP handshake.
const PROBE_TIMEOUT: Duration = Duration::from_millis(1500);
const FIREWALL_LIMIT: Duration = Duration::from_secs(2);
const TAILSCALE_LIMIT: Duration = Duration::from_secs(3);
/// All of the firewall questions together: a stalled firewall-cmd must not hold the page for ten seconds.
const FIREWALL_BUDGET: Duration = Duration::from_secs(5);
/// Peers probed for hosting at most: a tailnet with hundreds of shared devices is not scanned.
const MAX_PROBED_PEERS: usize = 64;
/// The most a program's output is read: a runaway one cannot fill memory.
const MAX_OUTPUT: u64 = 1 << 20;

const BAD_ADDRESS_HELP: &str = "Type an address like 192.0.2.10 or 192.0.2.10:47600";

/// Parses `addr` as IPv4 with an optional `:port`. The error is a sentence for the page.
fn parse_addr(addr: &str) -> Result<SocketAddr, String> {
    let text = addr.trim();
    if text.is_empty() {
        return Err(format!("No address typed. {BAD_ADDRESS_HELP}."));
    }
    let (host, port_text) = match text.split_once(':') {
        Some((h, p)) => (h, Some(p)),
        None => (text, None),
    };
    let ip: Ipv4Addr = host.parse().map_err(|_| format!("\"{text}\" is not an IPv4 address. {BAD_ADDRESS_HELP}."))?;
    let first = ip.octets()[0];
    if first == 0 || first >= 224 {
        return Err(format!("{ip} is not an address a host can have. {BAD_ADDRESS_HELP}."));
    }
    let port = match port_text {
        None => HOST_PORT,
        Some(p) => match p.parse::<u16>() {
            Ok(n) if n >= 1 => n,
            _ => return Err(format!("\"{p}\" is not a port from 1 to 65535. {BAD_ADDRESS_HELP}.")),
        },
    };
    Ok(SocketAddr::V4(SocketAddrV4::new(ip, port)))
}

/// Maps the outcome of a connection attempt to a result. Unreachable and every other failure count as no answer.
fn map_connect(res: io::Result<()>, ms: u64) -> ProbeResult {
    match res {
        Ok(()) => ProbeResult::Answered { ms },
        Err(e) => match e.kind() {
            io::ErrorKind::ConnectionRefused | io::ErrorKind::ConnectionReset => ProbeResult::Refused,
            _ => ProbeResult::TimedOut,
        },
    }
}

/// The probe with an injectable connector, so a timeout can be tested without a network.
fn probe_with(addr: &str, timeout: Duration, connect: &dyn Fn(SocketAddr, Duration) -> io::Result<()>) -> ProbeResult {
    let target = match parse_addr(addr) {
        Ok(t) => t,
        Err(message) => return ProbeResult::BadAddress { message },
    };
    let start = Instant::now();
    let res = connect(target, timeout);
    map_connect(res, start.elapsed().as_millis() as u64)
}

/// Tries one TCP connection to `addr`. Nothing is written or read; the stream is dropped at once.
pub fn probe(addr: &str) -> ProbeResult {
    probe_with(addr, PROBE_TIMEOUT, &|sock, limit| TcpStream::connect_timeout(&sock, limit).map(drop))
}

/// Runs a program with a time limit and returns (exited with success, stdout). A missing program is `ErrorKind::NotFound`,
/// a program that overstays is killed and reported as `ErrorKind::TimedOut`.
fn run_limited(program: &str, args: &[&str], limit: Duration) -> io::Result<(bool, String)> {
    let mut cmd = Command::new(program);
    cmd.args(args).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::null());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let mut child = cmd.spawn()?;
    let out = child.stdout.take();
    // the reader hands its text over a channel: a grandchild that keeps the pipe open can never make this function wait past its limit
    let (tx, rx) = std::sync::mpsc::channel::<String>();
    std::thread::spawn(move || {
        let mut s = String::new();
        if let Some(o) = out {
            let _ = o.take(MAX_OUTPUT).read_to_string(&mut s);
        }
        let _ = tx.send(s);
    });
    let start = Instant::now();
    let status = loop {
        match child.try_wait() {
            Ok(Some(st)) => break st,
            Ok(None) if start.elapsed() < limit => std::thread::sleep(Duration::from_millis(15)),
            Ok(None) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(io::Error::new(io::ErrorKind::TimedOut, "command took too long"));
            }
            Err(e) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(e);
            }
        }
    };
    let text = rx.recv_timeout(Duration::from_millis(500)).unwrap_or_default();
    Ok((status.success(), text))
}

/// `firewall-cmd --state` output to "running", "not_running" or "absent".
fn firewalld_state(res: &io::Result<(bool, String)>) -> &'static str {
    match res {
        Ok((true, out)) if out.trim() == "running" => "running",
        Err(e) if e.kind() == io::ErrorKind::NotFound => "absent",
        _ => "not_running",
    }
}

/// `firewall-cmd --get-active-zones`: zone names are the lines that do not start with whitespace.
fn parse_zones(out: &str) -> Vec<String> {
    out.lines().filter(|l| !l.is_empty() && !l.starts_with(char::is_whitespace)).map(|l| l.trim().to_string()).collect()
}

/// `firewall-cmd --list-ports`: space separated.
fn parse_ports(out: &str) -> Vec<String> {
    out.split_whitespace().map(str::to_string).collect()
}

/// Reads the firewall state, without privileges and without changing anything.
pub fn setup_facts() -> SetupFacts {
    let began = Instant::now();
    // each question gets what is left of the budget, at most FIREWALL_LIMIT
    let left = || FIREWALL_BUDGET.saturating_sub(began.elapsed()).min(FIREWALL_LIMIT);
    let state = firewalld_state(&run_limited("firewall-cmd", &["--state"], left()));
    let mut zones = Vec::new();
    let mut ports: Vec<String> = Vec::new();
    if state == "running" {
        if let Ok((true, out)) = run_limited("firewall-cmd", &["--get-active-zones"], left()) {
            zones = parse_zones(&out);
        }
        for z in &zones {
            if left().is_zero() {
                break;
            }
            let arg = format!("--zone={z}");
            if let Ok((true, out)) = run_limited("firewall-cmd", &[arg.as_str(), "--list-ports"], left()) {
                for p in parse_ports(&out) {
                    if !ports.contains(&p) {
                        ports.push(p);
                    }
                }
            }
        }
    }
    SetupFacts { firewalld: Firewalld { state: state.into(), zones, ports } }
}

fn ts_fail(error: &str) -> TailscaleReport {
    TailscaleReport { found: false, error: Some(error.into()), self_ips: vec![], peers: vec![] }
}

/// Only the IPv4 addresses (100.x) out of a `TailscaleIPs` array.
fn ipv4_list(v: &serde_json::Value) -> Vec<String> {
    v.get("TailscaleIPs")
        .and_then(|a| a.as_array())
        .map(|a| a.iter().filter_map(|x| x.as_str()).filter(|s| s.parse::<Ipv4Addr>().is_ok()).map(str::to_string).collect())
        .unwrap_or_default()
}

/// Parses `tailscale status --json`. `hosting` is left None everywhere; see `mark_hosting`.
fn parse_tailscale(json: &str) -> TailscaleReport {
    let v: serde_json::Value = match serde_json::from_str::<serde_json::Value>(json) {
        Ok(v) if v.is_object() => v,
        _ => return ts_fail("Tailscale did not answer in a way this launcher understands."),
    };
    match v.get("BackendState").and_then(|s| s.as_str()) {
        Some("Running") => {}
        _ => return ts_fail("Tailscale is not running or not signed in."),
    }
    let self_ips = v.get("Self").map(ipv4_list).unwrap_or_default();
    let mut peers = Vec::new();
    if let Some(map) = v.get("Peer").and_then(|p| p.as_object()) {
        for p in map.values() {
            let ips = ipv4_list(p);
            if ips.is_empty() {
                continue;
            }
            let text = |k: &str| p.get(k).and_then(|s| s.as_str()).unwrap_or("").to_string();
            peers.push(Peer {
                name: text("HostName"),
                dns_name: text("DNSName").trim_end_matches('.').to_string(),
                ips,
                online: p.get("Online").and_then(|b| b.as_bool()).unwrap_or(false),
                hosting: None,
            });
        }
    }
    TailscaleReport { found: true, error: None, self_ips, peers }
}

/// Probes each online peer's first address in parallel and sets `hosting`. The prober gets the IPv4 text and says whether
/// the host port answered.
fn mark_hosting(report: &mut TailscaleReport, prober: &(dyn Fn(&str) -> bool + Sync)) {
    std::thread::scope(|s| {
        let mut probed = 0;
        let handles: Vec<_> = report
            .peers
            .iter()
            .map(|p| {
                let ip = p.ips.first().cloned().filter(|_| p.online && probed < MAX_PROBED_PEERS);
                probed += usize::from(ip.is_some());
                (p.online, ip.map(|ip| s.spawn(move || prober(&ip))))
            })
            .collect();
        for (peer, (online, handle)) in report.peers.iter_mut().zip(handles) {
            peer.hosting = match (online, handle) {
                (true, Some(h)) => Some(h.join().unwrap_or(false)),
                _ => None,   // offline, or beyond MAX_PROBED_PEERS: not checked
            };
        }
    });
}

/// Reads the tailnet and probes the online peers.
pub fn tailscale_peers() -> TailscaleReport {
    let programs: &[&str] =
        if cfg!(windows) { &["tailscale.exe", r"C:\Program Files\Tailscale\tailscale.exe"] } else { &["tailscale"] };
    let mut found = None;
    for prog in programs {
        match run_limited(prog, &["status", "--json"], TAILSCALE_LIMIT) {
            Err(e) if e.kind() == io::ErrorKind::NotFound => continue,
            other => {
                found = Some(other);
                break;
            }
        }
    }
    let (ok, out) = match found {
        None => return ts_fail("Tailscale is not installed on this PC."),
        Some(Err(_)) => return ts_fail("Tailscale did not answer."),
        Some(Ok(r)) => r,
    };
    if !ok || out.trim().is_empty() {
        return ts_fail("Tailscale is not running or not signed in.");
    }
    let mut report = parse_tailscale(&out);
    if report.found {
        mark_hosting(&mut report, &|ip| matches!(probe(ip), ProbeResult::Answered { .. }));
    }
    report
}

#[cfg(test)]
mod shape_tests {
    //! The JSON the page reads (api.js, dev/mock.js). Both missions build on these strings: do not change them.
    use super::*;

    fn json<T: Serialize>(v: &T) -> String {
        serde_json::to_string(v).unwrap()
    }

    #[test]
    fn contract_probe_shapes() {
        assert_eq!(json(&ProbeResult::Answered { ms: 12 }), r#"{"kind":"answered","ms":12}"#);
        assert_eq!(json(&ProbeResult::Refused), r#"{"kind":"refused"}"#);
        assert_eq!(json(&ProbeResult::TimedOut), r#"{"kind":"timed_out"}"#);
        assert_eq!(json(&ProbeResult::BadAddress { message: "x".into() }), r#"{"kind":"bad_address","message":"x"}"#);
    }

    #[test]
    fn contract_setup_and_tailscale_shapes() {
        let f = SetupFacts { firewalld: Firewalld { state: "running".into(), zones: vec!["FedoraWorkstation".into()], ports: vec!["1025-65535/tcp".into()] } };
        assert_eq!(json(&f), r#"{"firewalld":{"state":"running","zones":["FedoraWorkstation"],"ports":["1025-65535/tcp"]}}"#);
        let t = TailscaleReport {
            found: true,
            error: None,
            self_ips: vec!["100.64.0.5".into()],
            peers: vec![Peer { name: "deck".into(), dns_name: "deck.t.ts.net".into(), ips: vec!["100.64.0.9".into()], online: true, hosting: None }],
        };
        assert_eq!(
            json(&t),
            r#"{"found":true,"self":["100.64.0.5"],"peers":[{"name":"deck","dns_name":"deck.t.ts.net","ips":["100.64.0.9"],"online":true,"hosting":null}]}"#
        );
        let missing = TailscaleReport { found: false, error: Some("not installed".into()), self_ips: vec![], peers: vec![] };
        assert_eq!(json(&missing), r#"{"found":false,"error":"not installed","self":[],"peers":[]}"#);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::TcpListener;
    use std::sync::Mutex;

    #[test]
    fn probe_answers_on_a_listening_port() {
        let l = TcpListener::bind("127.0.0.1:0").unwrap();
        let addr = l.local_addr().unwrap().to_string();
        assert!(matches!(probe(&addr), ProbeResult::Answered { .. }));
    }

    #[test]
    fn probe_refused_on_a_closed_port() {
        let l = TcpListener::bind("127.0.0.1:0").unwrap();
        let addr = l.local_addr().unwrap().to_string();
        drop(l);
        assert_eq!(probe(&addr), ProbeResult::Refused);
    }

    #[test]
    fn probe_maps_timeout_error() {
        let r = probe_with("192.0.2.10", Duration::from_millis(10), &|_, _| Err(io::Error::from(io::ErrorKind::TimedOut)));
        assert_eq!(r, ProbeResult::TimedOut);
    }

    #[test]
    fn probe_bad_address_says_why() {
        for bad in ["", "hello", "999.1.1.1", "192.0.2.10:0", "192.0.2.10:70000", "0.1.2.3", "224.0.0.1", "1.2.3.4:x"] {
            match probe(bad) {
                ProbeResult::BadAddress { message } => assert!(!message.is_empty(), "{bad}"),
                other => panic!("{bad:?} gave {other:?}"),
            }
        }
    }

    #[test]
    fn probe_default_port_is_47600() {
        let seen = Mutex::new(None);
        let r = probe_with("192.0.2.10", PROBE_TIMEOUT, &|a, _| {
            *seen.lock().unwrap() = Some(a);
            Ok(())
        });
        assert!(matches!(r, ProbeResult::Answered { .. }));
        assert_eq!(seen.lock().unwrap().unwrap().port(), 47600);
        let r = probe_with("192.0.2.10:5000", PROBE_TIMEOUT, &|a, _| {
            *seen.lock().unwrap() = Some(a);
            Ok(())
        });
        assert!(matches!(r, ProbeResult::Answered { .. }));
        assert_eq!(seen.lock().unwrap().unwrap().port(), 5000);
    }

    #[test]
    fn firewalld_parse_running_with_ports() {
        assert_eq!(firewalld_state(&Ok((true, "running\n".into()))), "running");
        assert_eq!(firewalld_state(&Ok((false, "not running\n".into()))), "not_running");
        assert_eq!(firewalld_state(&Ok((true, "not running\n".into()))), "not_running");
        let zones = parse_zones("FedoraWorkstation\n  interfaces: enp5s0\npublic\n  interfaces: wlan0\n");
        assert_eq!(zones, vec!["FedoraWorkstation", "public"]);
        assert_eq!(parse_ports("1025-65535/tcp 1025-65535/udp 47600/tcp\n"), vec!["1025-65535/tcp", "1025-65535/udp", "47600/tcp"]);
        assert!(parse_ports("\n").is_empty());
    }

    #[test]
    fn firewalld_absent_without_the_program() {
        let missing: io::Result<(bool, String)> = Err(io::Error::from(io::ErrorKind::NotFound));
        assert_eq!(firewalld_state(&missing), "absent");
        let other: io::Result<(bool, String)> = Err(io::Error::from(io::ErrorKind::PermissionDenied));
        assert_eq!(firewalld_state(&other), "not_running");
    }

    const NORMAL: &str = r#"{
      "BackendState": "Running",
      "Self": {"HostName": "me", "DNSName": "me.tail1.ts.net.", "Online": true, "TailscaleIPs": ["100.64.0.5", "fd7a:115c:a1e0::5"]},
      "Peer": {
        "nodekey:a": {"HostName": "deck", "DNSName": "deck.tail1.ts.net.", "Online": true, "TailscaleIPs": ["100.64.0.9", "fd7a:115c:a1e0::9"]},
        "nodekey:b": {"HostName": "laptop", "DNSName": "laptop.tail1.ts.net.", "Online": true, "TailscaleIPs": ["100.64.0.10"]},
        "nodekey:c": {"HostName": "v6only", "DNSName": "v6only.tail1.ts.net.", "Online": true, "TailscaleIPs": ["fd7a:115c:a1e0::11"]}
      }
    }"#;

    #[test]
    fn tailscale_parse_normal() {
        let r = parse_tailscale(NORMAL);
        assert!(r.found && r.error.is_none());
        assert_eq!(r.self_ips, vec!["100.64.0.5"]);
        assert_eq!(r.peers.len(), 2); // the IPv6-only peer is dropped
        let deck = r.peers.iter().find(|p| p.name == "deck").unwrap();
        assert_eq!(deck.dns_name, "deck.tail1.ts.net");
        assert_eq!(deck.ips, vec!["100.64.0.9"]);
        assert!(deck.online && deck.hosting.is_none());
    }

    #[test]
    fn tailscale_parse_no_peers() {
        let r = parse_tailscale(r#"{"BackendState":"Running","Self":{"TailscaleIPs":["100.64.0.5"]},"Peer":null}"#);
        assert!(r.found);
        assert_eq!(r.self_ips, vec!["100.64.0.5"]);
        assert!(r.peers.is_empty());
        let r = parse_tailscale(r#"{"BackendState":"Running","Self":{"TailscaleIPs":["100.64.0.5"]}}"#);
        assert!(r.found && r.peers.is_empty());
        let r = parse_tailscale(r#"{"BackendState":"Stopped"}"#);
        assert!(!r.found && r.error.is_some());
    }

    #[test]
    fn tailscale_parse_offline_peer() {
        let json = r#"{"BackendState":"Running","Self":{"TailscaleIPs":["100.64.0.5"]},
          "Peer":{"k":{"HostName":"off","DNSName":"off.t.ts.net.","Online":false,"TailscaleIPs":["100.64.0.20"]}}}"#;
        let mut r = parse_tailscale(json);
        assert_eq!(r.peers.len(), 1);
        assert!(!r.peers[0].online);
        mark_hosting(&mut r, &|_| true);
        assert_eq!(r.peers[0].hosting, None);
    }

    #[test]
    fn tailscale_parse_malformed() {
        for bad in ["", "   ", "not json at all", "[1,2,3]", "{\"x\":", "{}"] {
            let r = parse_tailscale(bad);
            assert!(!r.found, "{bad:?}");
            assert!(r.error.as_deref().is_some_and(|e| !e.is_empty()), "{bad:?}");
        }
    }

    #[test]
    fn tailscale_hosting_marks_listening_peers() {
        let mut r = parse_tailscale(NORMAL);
        mark_hosting(&mut r, &|ip| ip == "100.64.0.9");
        let by = |n: &str| r.peers.iter().find(|p| p.name == n).unwrap().hosting;
        assert_eq!(by("deck"), Some(true));
        assert_eq!(by("laptop"), Some(false));
    }

    #[test]
    fn tailscale_hosting_probes_a_bounded_number_of_peers() {
        let peers = (0..100)
            .map(|i| Peer { name: format!("p{i}"), dns_name: String::new(), ips: vec![format!("100.64.1.{i}")], online: true, hosting: None })
            .collect();
        let mut r = TailscaleReport { found: true, error: None, self_ips: vec![], peers };
        mark_hosting(&mut r, &|_| true);
        assert_eq!(r.peers.iter().filter(|p| p.hosting == Some(true)).count(), MAX_PROBED_PEERS);
        assert!(r.peers[MAX_PROBED_PEERS..].iter().all(|p| p.hosting.is_none()));
    }

    #[test]
    fn run_limited_returns_when_a_grandchild_holds_the_pipe() {
        // the shell exits at once, its background sleep keeps stdout open: the call must still return within its limit
        if cfg!(windows) {
            return;
        }
        let began = Instant::now();
        let res = run_limited("sh", &["-c", "sleep 3 & echo hi"], Duration::from_millis(800));
        assert!(began.elapsed() < Duration::from_millis(1800), "took {:?}", began.elapsed());
        assert!(res.is_ok());
    }
}
