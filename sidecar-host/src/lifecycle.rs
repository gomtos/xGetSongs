//! Where the sidecar is in its life, shared by the thread that starts it, the calls that wait for it and the exit of the app.
//! Starting takes seconds (a JVM), so nothing waits for it on the UI thread. No lock is held while a call runs: a call gets
//! its own `Bridge` (a copy of the port and the token) and the state is free again.

use crate::bridge::Bridge;
use crate::error::BridgeError;
use crate::sidecar::Sidecar;
use std::sync::{Arc, Condvar, Mutex, MutexGuard, PoisonError};
use std::time::{Duration, Instant};

enum Phase {
    Starting,
    Ready(Sidecar),
    Failed(String),
    Stopped,
}

struct Shared {
    phase: Mutex<Phase>,
    changed: Condvar,
}

#[derive(Clone)]
pub struct Lifecycle {
    shared: Arc<Shared>,
}

impl Lifecycle {
    /// Runs [start] on a thread of its own and keeps its result.
    pub fn start(start: impl FnOnce() -> Result<Sidecar, String> + Send + 'static) -> Lifecycle {
        let lifecycle =
            Lifecycle { shared: Arc::new(Shared { phase: Mutex::new(Phase::Starting), changed: Condvar::new() }) };
        let background = lifecycle.clone();
        std::thread::spawn(move || background.finish_start(start()));
        lifecycle
    }

    fn lock(&self) -> MutexGuard<'_, Phase> {
        self.shared.phase.lock().unwrap_or_else(PoisonError::into_inner)
    }

    fn finish_start(&self, result: Result<Sidecar, String>) {
        let late = {
            let mut phase = self.lock();
            let late = match (&*phase, result) {
                (Phase::Starting, Ok(sidecar)) => {
                    *phase = Phase::Ready(sidecar);
                    None
                }
                (Phase::Starting, Err(reason)) => {
                    *phase = Phase::Failed(reason);
                    None
                }
                // The app was closed while this was starting and gave up waiting: nobody will use this sidecar.
                (_, Ok(sidecar)) => Some(sidecar),
                (_, Err(_)) => None,
            };
            self.shared.changed.notify_all();
            late
        };
        if let Some(mut sidecar) = late {
            sidecar.shutdown(Duration::from_secs(3));
        }
    }

    /// Waits up to [timeout] for the sidecar to be ready and returns the means to call it.
    pub fn wait_ready(&self, timeout: Duration) -> Result<Bridge, BridgeError> {
        let deadline = Instant::now() + timeout;
        let mut phase = self.lock();
        loop {
            match &*phase {
                Phase::Ready(sidecar) => return Ok(Bridge::new(sidecar.handshake.clone())),
                Phase::Failed(reason) => {
                    return Err(BridgeError::new(format!("서버를 시작하지 못했습니다: {reason}")));
                }
                Phase::Stopped => return Err(BridgeError::new("서버가 종료되었습니다.")),
                Phase::Starting => {}
            }
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Err(BridgeError::new("서버가 아직 준비되지 않았습니다."));
            }
            phase = self.shared.changed.wait_timeout(phase, left).unwrap_or_else(PoisonError::into_inner).0;
        }
    }

    /// Ends the sidecar the way the user asked for it (an `exit` line, then [grace] to end by itself). A sidecar that is
    /// still starting is waited for, up to [start_wait], so that it is not left to the Job Object, which kills it without
    /// an exit record and makes the next start report a crash.
    pub fn shutdown(&self, grace: Duration, start_wait: Duration) {
        let sidecar = {
            let deadline = Instant::now() + start_wait;
            let mut phase = self.lock();
            while matches!(*phase, Phase::Starting) {
                let left = deadline.saturating_duration_since(Instant::now());
                if left.is_zero() {
                    break;
                }
                phase = self.shared.changed.wait_timeout(phase, left).unwrap_or_else(PoisonError::into_inner).0;
            }
            let before = std::mem::replace(&mut *phase, Phase::Stopped);
            self.shared.changed.notify_all();
            match before {
                Phase::Ready(sidecar) => Some(sidecar),
                _ => None,
            }
        };
        if let Some(mut sidecar) = sidecar {
            sidecar.shutdown(grace);
        }
    }
}

#[cfg(all(test, windows))]
mod tests {
    use super::*;
    use std::path::{Path, PathBuf};
    use std::process::Command;

    const READY: &str = "[Console]::Out.WriteLine('XGS-READY 4321 tok-lifecycle'); [Console]::Out.Flush();";

    fn log_path(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("xgs-lifecycle-test-{}-{}.log", std::process::id(), name))
    }

    /// A stand-in for the sidecar: PowerShell running [script].
    fn start_stand_in(script: String, name: &str) -> Lifecycle {
        let log = log_path(name);
        Lifecycle::start(move || {
            let mut command = Command::new("powershell");
            command.args(["-NoProfile", "-NonInteractive", "-Command", &script]);
            Sidecar::spawn(command, &log, Duration::from_secs(30))
        })
    }

    /// A stand-in that, once ready, writes the first line it reads from stdin to [result] (`eof` when the pipe just closes).
    fn script_that_records_the_first_stdin_line(result: &Path, delay_seconds: u32) -> String {
        format!(
            "Start-Sleep -Seconds {delay_seconds}; {READY} $line = [Console]::In.ReadLine(); if ($null -eq $line) {{ $line = 'eof' }}; [IO.File]::WriteAllText('{}', [string]$line)",
            result.display()
        )
    }

    fn read_when_written(path: &Path) -> String {
        let deadline = Instant::now() + Duration::from_secs(15);
        while Instant::now() < deadline {
            let content = std::fs::read_to_string(path).unwrap_or_default();
            if !content.is_empty() {
                return content;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        String::new()
    }

    #[test]
    fn a_call_that_comes_while_the_sidecar_starts_waits_and_then_gets_the_means_to_call_it() {
        let lifecycle = start_stand_in(format!("Start-Sleep -Seconds 1; {READY} Start-Sleep -Seconds 30"), "waits");

        let started = Instant::now();
        let bridge = lifecycle.wait_ready(Duration::from_secs(20)).unwrap();

        assert!(started.elapsed() >= Duration::from_millis(500), "it did wait: {:?}", started.elapsed());
        assert!(!format!("{bridge:?}").contains("tok-lifecycle"), "the token is not printed");
        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
    }

    #[test]
    fn several_calls_that_wait_are_all_released_when_the_sidecar_is_ready() {
        let lifecycle = start_stand_in(format!("Start-Sleep -Seconds 1; {READY} Start-Sleep -Seconds 30"), "released");

        let waiters: Vec<_> = (0..3)
            .map(|_| {
                let lifecycle = lifecycle.clone();
                std::thread::spawn(move || lifecycle.wait_ready(Duration::from_secs(20)).is_ok())
            })
            .collect();

        assert!(waiters.into_iter().all(|waiter| waiter.join().unwrap()));
        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
    }

    #[test]
    fn a_sidecar_that_could_not_start_is_reported_with_its_reason() {
        let lifecycle = Lifecycle::start(|| Err("no runtime".to_string()));

        let error = lifecycle.wait_ready(Duration::from_secs(10)).unwrap_err();

        assert!(error.message.contains("서버를 시작하지 못했습니다"), "{error}");
        assert!(error.message.contains("no runtime"), "{error}");
    }

    #[test]
    fn a_call_gives_up_waiting_after_its_limit() {
        let lifecycle = Lifecycle::start(|| {
            std::thread::sleep(Duration::from_secs(3));
            Err("late".to_string())
        });
        let started = Instant::now();

        let error = lifecycle.wait_ready(Duration::from_millis(300)).unwrap_err();

        assert!(error.message.contains("아직 준비되지 않았습니다"), "{error}");
        assert!(started.elapsed() < Duration::from_secs(2), "{:?}", started.elapsed());
    }

    #[test]
    fn after_the_shutdown_the_sidecar_got_the_exit_line_and_calls_are_refused() {
        let result = log_path("exit-line");
        let _ = std::fs::remove_file(&result);
        let lifecycle = start_stand_in(script_that_records_the_first_stdin_line(&result, 0), "exit-line-err");
        lifecycle.wait_ready(Duration::from_secs(20)).unwrap();

        lifecycle.shutdown(Duration::from_secs(10), Duration::from_secs(5));

        assert_eq!(read_when_written(&result), "exit");
        assert_eq!(lifecycle.wait_ready(Duration::from_secs(1)).unwrap_err().message, "서버가 종료되었습니다.");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn closing_while_the_sidecar_is_still_starting_waits_for_it_and_ends_it_properly() {
        let result = log_path("closed-while-starting");
        let _ = std::fs::remove_file(&result);
        let lifecycle = start_stand_in(script_that_records_the_first_stdin_line(&result, 2), "closed-while-starting-err");

        lifecycle.shutdown(Duration::from_secs(10), Duration::from_secs(20));

        assert_eq!(read_when_written(&result), "exit", "it was told the user closed the app, not left to the Job Object");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn a_sidecar_that_comes_after_the_wait_for_it_was_given_up_is_still_ended_properly() {
        let result = log_path("late");
        let _ = std::fs::remove_file(&result);
        let lifecycle = start_stand_in(script_that_records_the_first_stdin_line(&result, 2), "late-err");

        lifecycle.shutdown(Duration::from_secs(10), Duration::from_millis(100));

        assert_eq!(lifecycle.wait_ready(Duration::from_secs(1)).unwrap_err().message, "서버가 종료되었습니다.");
        assert_eq!(read_when_written(&result), "exit");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn a_shutdown_that_comes_twice_does_no_harm() {
        let lifecycle = start_stand_in(format!("{READY} Start-Sleep -Seconds 30"), "twice");
        lifecycle.wait_ready(Duration::from_secs(20)).unwrap();

        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
    }
}
