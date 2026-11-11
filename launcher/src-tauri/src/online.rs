// SPDX-License-Identifier: GPL-3.0-or-later
//! Online presence: while pkp.exe runs, and only when the player switched it on, tells api.recompia.com that this player is online
//! (hello, then a heartbeat every half minute, then leave). What is sent is exactly what the setting's notice and recompia.com say: the
//! online name the player typed (none: the server shows "anonymous"), the build, the kind of device and the Pokemon-at-home count of the
//! save (home_count.rs). Never the save, the profile nickname or the account uid.
//!
//! Identity: the token is SHA-256 of the profile uid and a random per-install salt (config.rs online_salt), so the server sees a stable
//! pseudonym per profile that cannot be turned back into the uid. "Reset my online ID" empties the salt; nothing else changes it.
//!
//! Best effort and silent, like presence.rs: a server that is down or slow only means no presence. Requests have short timeouts, the
//! game never waits on this, and `Watch::finish` gives the leave a few seconds, then lets go (the server marks a silent player offline
//! after 90 s anyway).

use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver, RecvTimeoutError, Sender};
use std::time::Duration;

/// The presence API (jerbanpls/recompia-website api/). Public reads; writes need the token.
pub const SERVER: &str = "https://api.recompia.com";
const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
const IO_TIMEOUT: Duration = Duration::from_secs(10);
/// The server asks for a heartbeat every `heartbeat_interval` seconds; an answer outside this range is not trusted.
const MIN_INTERVAL: Duration = Duration::from_secs(10);
const MAX_INTERVAL: Duration = Duration::from_secs(120);
const DEFAULT_INTERVAL: Duration = Duration::from_secs(30);
/// How long `finish` waits for the leave before it lets go.
const LEAVE_WAIT: Duration = Duration::from_secs(3);

/// Who is online: everything a hello sends. Made once per game run.
#[derive(Clone, Debug, PartialEq)]
pub struct Player {
    pub token: String,
    /// The online name; None: anonymous.
    pub name: Option<String>,
    pub build: String,
    pub device: &'static str,
}

/// The online ID: SHA-256 of the profile uid and the install's salt, as hex.
pub fn token_of(uid: &str, salt: &str) -> String {
    let mut h = Sha256::new();
    h.update(b"recompia-online-v1\0");
    h.update(uid.as_bytes());
    h.update(b"\0");
    h.update(salt.as_bytes());
    h.finalize().iter().map(|b| format!("{:02x}", b)).collect()
}

/// The device word the server shows: the launcher's own platform.
pub fn device() -> &'static str {
    if cfg!(windows) { "windows" } else { "linux" }
}

impl Player {
    pub fn new(uid: &str, salt: &str, name: &str, build: &str) -> Player {
        let name = name.trim();
        Player { token: token_of(uid, salt), name: (!name.is_empty()).then(|| name.to_string()), build: build.to_string(), device: device() }
    }
}

/// The player for this run, when online presence is switched on: the active profile's uid with the install's salt, the online name
/// and this launcher's version. None when it is off or no salt could be saved.
pub fn player_of(app: &tauri::AppHandle, game_dir: &Path, cfg: &crate::config::Config) -> Option<Player> {
    if !cfg.online_presence {
        return None;
    }
    let salt = crate::config::online_salt(app)?;
    let uid = crate::profiles::load(game_dir).active_profile().uid.clone();
    Some(Player::new(&uid, &salt, &cfg.online_name, &app.package_info().version.to_string()))
}

enum Failure {
    /// The server answered with this status (404/409 on a heartbeat: the session is gone, say hello again).
    Status(u16),
    /// No answer, or one that is not JSON.
    Network,
}

struct Client {
    base: String,
    agent: ureq::Agent,
    token: String,
}

impl Client {
    fn new(base: &str, token: &str) -> Client {
        let agent = ureq::AgentBuilder::new()
            .timeout_connect(CONNECT_TIMEOUT)
            .timeout_read(IO_TIMEOUT)
            .timeout_write(IO_TIMEOUT)
            .user_agent(&format!("Recompia-Launcher/{}", env!("CARGO_PKG_VERSION")))
            .build();
        Client { base: base.trim_end_matches('/').to_string(), agent, token: token.to_string() }
    }

    fn post(&self, path: &str, body: &Value) -> Result<Value, Failure> {
        let res = self
            .agent
            .post(&format!("{}{}", self.base, path))
            .set("Authorization", &format!("Bearer {}", self.token))
            .set("Content-Type", "application/json")
            .send_string(&body.to_string());
        match res {
            Ok(r) => r.into_string().ok().and_then(|t| serde_json::from_str(&t).ok()).ok_or(Failure::Network),
            Err(ureq::Error::Status(code, _)) => Err(Failure::Status(code)),
            Err(_) => Err(Failure::Network),
        }
    }

    /// A new session: its id and the heartbeat interval the server asks for.
    fn hello(&self, p: &Player, count: Option<u32>) -> Option<(String, Duration)> {
        let body = json!({ "token": p.token, "nickname": p.name, "build": p.build, "device": p.device, "pokemon_in_home": count });
        let r = self.post("/v1/hello", &body).ok()?;
        let sid = r["session_id"].as_str()?.to_string();
        let every = r["heartbeat_interval"].as_u64().map_or(DEFAULT_INTERVAL, Duration::from_secs);
        Some((sid, every))
    }
}

/// One game run's presence. `count` reads the Pokemon-at-home count (once per hello and heartbeat); `floor` is the shortest heartbeat
/// interval accepted (MIN_INTERVAL; the tests use less).
fn run(client: &Client, p: &Player, count: &dyn Fn() -> Option<u32>, stop: &Receiver<()>, floor: Duration) {
    let mut session: Option<String> = None;
    let mut every = DEFAULT_INTERVAL;
    loop {
        match &session {
            None => {
                if let Some((sid, e)) = client.hello(p, count()) {
                    session = Some(sid);
                    every = e.clamp(floor, MAX_INTERVAL.max(floor));
                }
            }
            Some(sid) => {
                let body = json!({ "session_id": sid, "pokemon_in_home": count() });
                if let Err(Failure::Status(404 | 409)) = client.post("/v1/heartbeat", &body) {
                    session = None;   // the server let the session go (offline after silence, or restarted): a new hello next
                    continue;
                }
            }
        }
        match stop.recv_timeout(every) {
            Err(RecvTimeoutError::Timeout) => continue,
            _ => break,
        }
    }
    if let Some(sid) = session {
        let _ = client.post("/v1/leave", &json!({ "session_id": sid }));
    }
}

/// Presence for the length of one game run: started when pkp.exe starts, finished when it has ended. Does nothing without a player
/// (switched off, or no online ID could be made).
pub struct Watch {
    live: Option<(Sender<()>, Receiver<()>)>,
}

impl Watch {
    pub fn start(player: Option<Player>, game_dir: &Path) -> Watch {
        Watch::start_at(SERVER, player, game_dir.to_path_buf(), MIN_INTERVAL)
    }

    fn start_at(base: &str, player: Option<Player>, game_dir: PathBuf, floor: Duration) -> Watch {
        let Some(player) = player else { return Watch { live: None } };
        let (stop_tx, stop_rx) = channel();
        let (done_tx, done_rx) = channel();
        let client = Client::new(base, &player.token);
        std::thread::spawn(move || {
            run(&client, &player, &|| crate::home_count::home_count(&game_dir), &stop_rx, floor);
            let _ = done_tx.send(());
        });
        Watch { live: Some((stop_tx, done_rx)) }
    }

    /// The game has ended: says leave. A server that does not answer is let go after a few seconds.
    pub fn finish(self) {
        if let Some((stop, done)) = self.live {
            let _ = stop.send(());
            let _ = done.recv_timeout(LEAVE_WAIT);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{BufRead, BufReader, Read, Write};
    use std::net::TcpListener;
    use std::sync::{Arc, Mutex};

    /// What a stub server was sent: (path, Authorization header, JSON body).
    type Seen = Arc<Mutex<Vec<(String, String, Value)>>>;

    /// A presence API on a local port. `answer(path, n)` gives the status and JSON for the n-th request (0-based).
    fn stub(answer: fn(&str, usize) -> (u16, Value)) -> (String, Seen) {
        let l = TcpListener::bind("127.0.0.1:0").unwrap();
        let base = format!("http://{}", l.local_addr().unwrap());
        let seen: Seen = Arc::default();
        let log = seen.clone();
        std::thread::spawn(move || {
            for (n, s) in l.incoming().flatten().enumerate() {
                let mut r = BufReader::new(s.try_clone().unwrap());
                let mut line = String::new();
                r.read_line(&mut line).unwrap();
                let path = line.split_whitespace().nth(1).unwrap_or_default().to_string();
                let (mut len, mut auth) = (0, String::new());
                loop {
                    let mut h = String::new();
                    r.read_line(&mut h).unwrap();
                    if h.trim().is_empty() {
                        break;
                    }
                    let (k, v) = h.split_once(':').unwrap();
                    match k.to_ascii_lowercase().as_str() {
                        "content-length" => len = v.trim().parse().unwrap(),
                        "authorization" => auth = v.trim().to_string(),
                        _ => {}
                    }
                }
                let mut body = vec![0; len];
                r.read_exact(&mut body).unwrap();
                log.lock().unwrap().push((path.clone(), auth, serde_json::from_slice(&body).unwrap()));
                let (code, json) = answer(&path, n);
                let text = json.to_string();
                let mut s = s;
                let _ = write!(s, "HTTP/1.1 {} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", code, text.len(), text);
            }
        });
        (base, seen)
    }

    fn ok(path: &str, _: usize) -> (u16, Value) {
        match path {
            "/v1/hello" => (200, json!({ "session_id": "s1", "server_time": 1, "heartbeat_interval": 0 })),
            _ => (200, json!({ "ok": true })),
        }
    }

    fn player() -> Player {
        Player::new("0123456789abcdef0123456789abcdef", "salt", "  Misty ", "0.10.4")
    }

    fn run_until(base: &str, p: &Player, seen: &Seen, requests: usize) {
        let client = Client::new(base, &p.token);
        let (tx, rx) = channel();
        let log = seen.clone();
        std::thread::spawn(move || {
            while log.lock().unwrap().len() < requests {
                std::thread::sleep(Duration::from_millis(5));
            }
            let _ = tx.send(());
        });
        run(&client, p, &|| Some(64), &rx, Duration::from_millis(20));
    }

    #[test]
    fn the_token_is_a_stable_hash_that_hides_the_uid_and_changes_with_the_salt() {
        let a = token_of("0123456789abcdef0123456789abcdef", "salt");
        assert_eq!(a, token_of("0123456789abcdef0123456789abcdef", "salt"));
        assert_eq!(a.len(), 64);
        assert!(!a.contains("0123456789abcdef"));
        assert_ne!(a, token_of("0123456789abcdef0123456789abcdef", "other salt"));
        assert_ne!(a, token_of("fedcba9876543210fedcba9876543210", "salt"));
    }

    #[test]
    fn an_empty_online_name_is_anonymous() {
        assert_eq!(Player::new("u", "s", "   ", "b").name, None);
        assert_eq!(player().name.as_deref(), Some("Misty"));
    }

    #[test]
    fn hello_heartbeat_leave_send_only_what_the_notice_says() {
        let (base, seen) = stub(ok);
        let p = player();
        run_until(&base, &p, &seen, 2);
        let seen = seen.lock().unwrap();
        let paths: Vec<&str> = seen.iter().map(|s| s.0.as_str()).collect();
        assert_eq!(paths.first(), Some(&"/v1/hello"));
        assert!(paths[1..paths.len() - 1].iter().all(|p| *p == "/v1/heartbeat"), "{:?}", paths);
        assert_eq!(paths.last(), Some(&"/v1/leave"));
        assert!(seen.iter().all(|s| s.1 == format!("Bearer {}", p.token)));
        let hello = &seen[0].2;
        let mut keys: Vec<&str> = hello.as_object().unwrap().keys().map(|k| k.as_str()).collect();
        keys.sort();
        assert_eq!(keys, ["build", "device", "nickname", "pokemon_in_home", "token"]);
        assert_eq!(hello["nickname"], "Misty");
        assert_eq!(hello["pokemon_in_home"], 64);
        assert_eq!(hello["device"], device());
        assert_eq!(seen[1].2, json!({ "session_id": "s1", "pokemon_in_home": 64 }));
        assert_eq!(seen.last().unwrap().2, json!({ "session_id": "s1" }));
    }

    #[test]
    fn a_lost_session_says_hello_again() {
        fn gone_once(path: &str, n: usize) -> (u16, Value) {
            if path == "/v1/heartbeat" && n == 1 { (409, json!({ "error": "offline" })) } else { ok(path, n) }
        }
        let (base, seen) = stub(gone_once);
        run_until(&base, &player(), &seen, 3);
        let seen = seen.lock().unwrap();
        assert_eq!(seen[0].0, "/v1/hello");
        assert_eq!(seen[1].0, "/v1/heartbeat");
        assert_eq!(seen[2].0, "/v1/hello");
    }

    #[test]
    fn no_server_is_silent_and_finish_returns_quickly() {
        let l = TcpListener::bind("127.0.0.1:0").unwrap();
        let base = format!("http://{}", l.local_addr().unwrap());
        drop(l);   // nothing listens there now
        let t = std::time::Instant::now();
        Watch::start_at(&base, Some(player()), std::env::temp_dir(), Duration::from_millis(20)).finish();
        assert!(t.elapsed() < LEAVE_WAIT + Duration::from_secs(1));
    }

    #[test]
    fn off_means_no_thread_and_finish_returns_at_once() {
        Watch::start(None, Path::new(".")).finish();
    }
}
