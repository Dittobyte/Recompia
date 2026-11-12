//! Running the shader translator (tools/shaders/harness, `spirv_harness.exe`) for `shaders`: a run, a worker that runs chunk after
//! chunk in one process, and why a failed run failed.
use std::fs;
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::time::Duration;

use crate::shaders::{run_tool, LogFn};

/// What the harness writes in its `--out` folder when a run fails (`ErrorFile` in tools/shaders/harness/Program.cs).
const HARNESS_ERROR: &str = "harness_error.txt";

/// What the harness writes in its `--out` folder as a run goes (`ProgressFile` in tools/shaders/harness/Program.cs).
const HARNESS_PROGRESS: &str = "harness_progress.txt";
/// The translator jobs of a safe run.
const SAFE_JOBS: usize = 2;

/// How the translator runs. Fast: server GC (the harness's own default; with workstation GC each collection stops every worker,
/// about 4x slower) and every job. Safe: workstation GC and at most SAFE_JOBS jobs, for a translator that died under the fast
/// settings without a report (#530: exit status 2 about 3 s after start, every time, on a Steam Deck with 11.5 GB under Proton).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode {
    Fast,
    Safe,
}

/// The translator over `input` (a corpus folder with manifest.tsv / unique.tsv) into `stage`.
fn command(exe: &Path, input: &Path, stage: &Path, jobs: usize, mode: Mode) -> Result<Command, String> {
    let jobs = if mode == Mode::Safe { jobs.clamp(1, SAFE_JOBS) } else { jobs.max(1) };
    // the package's own build for this platform, run as it is: spirv_harness.exe on Windows, the native apphost elsewhere
    let mut cmd = crate::host::command(exe);
    cmd.arg("--batch").arg(input).arg("--out").arg(stage);
    cmd.args(["--jobs", &jobs.to_string(), "--no-val"]);
    cmd.env("DOTNET_gcServer", if mode == Mode::Fast { "1" } else { "0" });
    Ok(cmd)
}

/// A failed run: the message for setup.log, and whether the harness wrote an error report (an error it caught, not a process that
/// went without a word).
pub(crate) struct Failure {
    pub message: String,
    pub reported: bool,
}

/// Why a run failed, from what it leaves in `stage`: its own error file, or, with no `stage` folder at all, that it never started,
/// and the last steps of its progress file. (Under Proton its stdout and stderr never reach the launcher, #366.)
fn explain(stage: &Path, why: String) -> Failure {
    let report = fs::read_to_string(stage.join(HARNESS_ERROR)).ok();
    let mut text = match &report {
        Some(t) => format!("The shader translator reported:\n{}", t.trim()),
        None if !stage.is_dir() => {
            "The shader translator never started (it creates its output folder first): the program that runs it failed before it."
                .to_string()
        }
        None => "The shader translator started but wrote no error report.".to_string(),
    };
    if let Ok(p) = fs::read_to_string(stage.join(HARNESS_PROGRESS)) {
        let steps: Vec<&str> = p.lines().filter(|l| !l.trim().is_empty()).collect();
        text.push_str("\nIts last steps:");
        for l in &steps[steps.len().saturating_sub(4)..] {
            text.push_str("\n  ");
            text.push_str(l);
        }
    }
    Failure { message: format!("{}\n{}", why, text), reported: report.is_some() }
}

/// `run_tool` for the shader translator writing to `stage`, with a failure explained from `stage`.
fn run_once(cmd: Command, stage: &Path, cancel: &AtomicBool, idle: bool, log: Option<&LogFn>, line: impl FnMut(&str)) -> Result<bool, Failure> {
    run_tool(cmd, cancel, idle, log, None, line).map_err(|e| {
        let f = explain(stage, e);
        if let Some(l) = log {
            l(&f.message);
        }
        f
    })
}

/// One translator run over `input` into `stage` (emptied first). A run that dies without a report under `Mode::Fast` is run again
/// once under `Mode::Safe`, and `mode` stays Safe for the caller's later runs. Ok(false): cancelled.
#[allow(clippy::too_many_arguments)]
pub fn run(
    exe: &Path,
    input: &Path,
    stage: &Path,
    jobs: usize,
    mode: &mut Mode,
    cancel: &AtomicBool,
    idle: bool,
    log: Option<&LogFn>,
    mut line: impl FnMut(&str),
) -> Result<bool, String> {
    let _ = fs::remove_dir_all(stage);
    match run_once(command(exe, input, stage, jobs, *mode)?, stage, cancel, idle, log, &mut line) {
        Err(f) if !f.reported && *mode == Mode::Fast => {
            *mode = Mode::Safe;
            if let Some(l) = log {
                l(&format!("The shader translator stopped without a report: running it again with workstation GC and {} jobs, and keeping that.", jobs.clamp(1, SAFE_JOBS)));
            }
            let _ = fs::remove_dir_all(stage);
            run_once(command(exe, input, stage, jobs, Mode::Safe)?, stage, cancel, idle, log, &mut line).map_err(|f| f.message)
        }
        r => r.map_err(|f| f.message),
    }
}

/// One translator process for a whole translation (`--serve` in tools/shaders/harness/Program.cs): the launcher names each chunk's
/// corpus folder on its stdin and waits for `chunk done`, so a chunk no longer pays the .NET start, the cold JIT and the thread pool
/// ramp (about 3 s of every chunk on a 24-thread PC, and every one of them on a Steam Deck).
pub struct Worker {
    child: Child,
    stdin: Option<ChildStdin>,
    lines: mpsc::Receiver<String>,
    stderr: mpsc::Receiver<String>,
    name: String,
    stage: PathBuf,
    /// the last lines it printed, for a failure message
    tail: Vec<String>,
}

impl Worker {
    /// Starts the translator over `stage` in `mode`.
    pub fn start(exe: &Path, stage: &Path, jobs: usize, mode: Mode, idle: bool) -> Result<Worker, String> {
        let jobs = if mode == Mode::Safe { jobs.clamp(1, SAFE_JOBS) } else { jobs.max(1) };
        // the package's own build for this platform, run as it is: spirv_harness.exe on Windows, the native apphost elsewhere
        let mut cmd = crate::host::command(exe);
        cmd.arg("--serve").arg("--out").arg(stage).args(["--jobs", &jobs.to_string(), "--no-val"]);
        cmd.env("DOTNET_gcServer", if mode == Mode::Fast { "1" } else { "0" });
        crate::shaders::quiet(&mut cmd, idle);
        let name = exe.to_string_lossy().to_string();
        let mut child = cmd
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|e| format!("could not start {}: {}", name, e))?;
        let out = child.stdout.take().unwrap();
        let (tx, lines) = mpsc::channel::<String>();
        std::thread::spawn(move || BufReader::new(out).lines().map_while(Result::ok).for_each(|l| drop(tx.send(l))));
        let err = child.stderr.take().unwrap();
        let (etx, stderr) = mpsc::channel::<String>();
        std::thread::spawn(move || {
            let mut tail: Vec<String> = Vec::new();
            for l in BufReader::new(err).lines().map_while(Result::ok) {
                tail.push(l);
                if tail.len() > 12 {
                    tail.remove(0);
                }
            }
            let _ = etx.send(tail.join("\n"));
        });
        let stdin = child.stdin.take();
        Ok(Worker { child, stdin, lines, stderr, name, stage: stage.to_path_buf(), tail: Vec::new() })
    }

    /// Translates one chunk: `input` is the corpus folder holding its unique.tsv. Ok(true) finished, Ok(false) paused, Err: the
    /// process is gone (the caller starts another one).
    pub fn run_chunk(&mut self, input: &Path, cancel: &AtomicBool, log: Option<&LogFn>, mut line: impl FnMut(&str)) -> Result<bool, Failure> {
        let sent = match self.stdin.as_mut() {
            Some(stdin) => writeln!(stdin, "{}", input.to_string_lossy()).and_then(|()| stdin.flush()),
            None => Err(std::io::Error::new(std::io::ErrorKind::BrokenPipe, "its input is closed")),
        };
        if let Err(e) = sent {
            return Err(explain(&self.stage, format!("cannot send a chunk to {}: {}", self.name, e)));
        }
        loop {
            match self.lines.recv_timeout(Duration::from_millis(200)) {
                Ok(l) => {
                    match l.trim() {
                        "chunk done" => return Ok(true),
                        "chunk failed" => return Err(explain(&self.stage, format!("{} reported a failure on this chunk", self.name))),
                        _ => {}
                    }
                    if !crate::shaders::is_progress_line(&l) {
                        if let Some(f) = log {
                            f(&format!("out: {}", l));
                        }
                    }
                    self.tail.push(l.clone());
                    if self.tail.len() > 12 {
                        self.tail.remove(0);
                    }
                    line(&l);
                }
                Err(mpsc::RecvTimeoutError::Timeout) => {}
                Err(mpsc::RecvTimeoutError::Disconnected) => {
                    let status = self.child.wait().map(|s| s.to_string()).unwrap_or_else(|e| e.to_string());
                    let tail = self.stderr.recv_timeout(Duration::from_secs(5)).unwrap_or_default();
                    let why = format!("{} stopped ({}). Last output:\n{}\nLast error output:\n{}", self.name, status, self.tail.join("\n"), tail);
                    return Err(explain(&self.stage, why));
                }
            }
            if cancel.load(Ordering::SeqCst) {
                // at once, not after the chunk: a pause the player pressed has to stop now (what the chunk finished is already in the
                // stage, and that is what gets committed)
                self.kill();
                return Ok(false);
            }
        }
    }

    /// Ends the process at once.
    pub fn kill(&mut self) {
        self.stdin = None;
        crate::host::kill(&mut self.child);
        let _ = self.child.wait();
    }

    /// Ends the process: `quit` first, then a kill if it does not go.
    pub fn stop(&mut self) {
        if let Some(mut stdin) = self.stdin.take() {
            let _ = writeln!(stdin, "quit");
            let _ = stdin.flush();
        }
        let t = std::time::Instant::now();
        while t.elapsed() < Duration::from_secs(10) {
            if matches!(self.child.try_wait(), Ok(Some(_))) {
                return;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        crate::host::kill(&mut self.child);
        let _ = self.child.wait();
    }
}
