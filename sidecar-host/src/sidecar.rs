use crate::handshake::{parse_handshake, Handshake};
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::sync::mpsc::{self, Sender};
use std::time::{Duration, Instant};

/// Windows: do not open a console window for the child.
#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

/// A running sidecar. Dropping it closes its stdin, which asks the sidecar to stop on its own.
pub struct Sidecar {
    child: Child,
    stdin: Option<ChildStdin>,
    pub handshake: Handshake,
}

impl Sidecar {
    /// Starts [command] and waits up to [ready_timeout] for its handshake line. The child is killed when the line does
    /// not come. What the child writes to stderr goes to [stderr_log].
    pub fn spawn(mut command: Command, stderr_log: &Path, ready_timeout: Duration) -> Result<Sidecar, String> {
        command.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(CREATE_NO_WINDOW);
        }
        let mut child = command.spawn().map_err(|error| format!("could not start the sidecar: {error}"))?;
        let stdin = child.stdin.take();
        let stdout = child.stdout.take().expect("stdout is piped");
        let stderr = child.stderr.take().expect("stderr is piped");

        let log = stderr_log.to_path_buf();
        std::thread::spawn(move || copy_to_file(stderr, &log));
        let (sender, receiver) = mpsc::channel();
        std::thread::spawn(move || drain_stdout(stdout, sender));

        match receiver.recv_timeout(ready_timeout) {
            Ok(handshake) => Ok(Sidecar { child, stdin, handshake }),
            Err(error) => {
                kill_tree(child.id());
                let _ = child.kill();
                let _ = child.wait();
                Err(match error {
                    mpsc::RecvTimeoutError::Timeout => {
                        format!("the sidecar did not say it was ready within {} s", ready_timeout.as_secs())
                    }
                    mpsc::RecvTimeoutError::Disconnected => "the sidecar ended before it said it was ready".to_string(),
                })
            }
        }
    }

    /// Ends the sidecar: the user asked for it, so an `exit` line is written before stdin is closed (a pipe that only
    /// closes tells the sidecar that the shell is gone), and after [grace] it is killed together with whatever it started.
    /// Returns true when it ended on its own.
    pub fn shutdown(&mut self, grace: Duration) -> bool {
        if let Some(stdin) = self.stdin.as_mut() {
            let _ = stdin.write_all(b"exit\n");
            let _ = stdin.flush();
        }
        drop(self.stdin.take());
        let deadline = Instant::now() + grace;
        loop {
            match self.child.try_wait() {
                Ok(Some(_)) => return true,
                Ok(None) if Instant::now() < deadline => std::thread::sleep(Duration::from_millis(25)),
                _ => break,
            }
        }
        kill_tree(self.child.id());
        let _ = self.child.kill();
        let _ = self.child.wait();
        false
    }
}

/// Reads stdout to its end: the handshake line goes to [sender], everything else is thrown away. Reading on after the
/// handshake matters: a child whose stdout pipe is full blocks in its next write.
fn drain_stdout(stdout: ChildStdout, sender: Sender<Handshake>) {
    let mut reader = BufReader::new(stdout);
    let mut line = Vec::new();
    loop {
        line.clear();
        match reader.read_until(b'\n', &mut line) {
            Ok(0) | Err(_) => break,
            Ok(_) => {
                if let Some(handshake) = parse_handshake(&String::from_utf8_lossy(&line)) {
                    let _ = sender.send(handshake);
                }
            }
        }
    }
}

/// Keeps what the child writes to stderr in a file (spike: it is not rotated). When the file cannot be made the data is
/// still read and dropped, so the child never blocks on a full pipe.
fn copy_to_file(mut source: impl Read, path: &PathBuf) {
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    match std::fs::File::create(path) {
        Ok(mut file) => {
            let _ = std::io::copy(&mut source, &mut file);
        }
        Err(_) => {
            let _ = std::io::copy(&mut source, &mut std::io::sink());
        }
    }
}

/// Ends [pid] and every process it started (`kill()` alone ends only the process itself).
#[cfg(windows)]
fn kill_tree(pid: u32) {
    use std::os::windows::process::CommandExt;
    let pid = pid.to_string();
    let _ = Command::new("taskkill")
        .args(["/T", "/F", "/PID", pid.as_str()])
        .creation_flags(CREATE_NO_WINDOW)
        .output();
}

#[cfg(not(windows))]
fn kill_tree(_pid: u32) {}

#[cfg(all(test, windows))]
mod tests {
    use super::*;
    use std::time::Duration;

    /// A stand-in for the sidecar: PowerShell running [script].
    fn powershell(script: &str) -> Command {
        let mut command = Command::new("powershell");
        command.args(["-NoProfile", "-NonInteractive", "-Command", script]);
        command
    }

    const READY: &str = "[Console]::Out.WriteLine('XGS-READY 4321 tok'); [Console]::Out.Flush();";
    const WAIT_FOR_STDIN_TO_END: &str = "[Console]::In.ReadToEnd() | Out-Null";

    fn log_path(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("xgs-sidecar-test-{}-{}.log", std::process::id(), name))
    }

    #[test]
    fn reads_the_handshake_among_other_lines_and_ends_when_stdin_closes() {
        let script = format!("[Console]::Out.WriteLine('noise 1'); {READY} {WAIT_FOR_STDIN_TO_END}");

        let mut sidecar =
            Sidecar::spawn(powershell(&script), &log_path("noise"), Duration::from_secs(30)).unwrap();

        assert_eq!(sidecar.handshake, Handshake { port: 4321, token: "tok".to_string() });
        assert!(sidecar.shutdown(Duration::from_secs(10)), "it ends on its own when stdin closes");
    }

    #[test]
    fn keeps_reading_stdout_after_the_handshake_so_the_sidecar_never_blocks() {
        // 5000 lines of 100 characters are far more than a pipe holds: a sidecar whose stdout is not read blocks in WriteLine.
        let script = format!(
            "{READY} 1..5000 | ForEach-Object {{ [Console]::Out.WriteLine(('x' * 100)) }}; {WAIT_FOR_STDIN_TO_END}"
        );

        let mut sidecar =
            Sidecar::spawn(powershell(&script), &log_path("drain"), Duration::from_secs(30)).unwrap();
        std::thread::sleep(Duration::from_millis(2000));

        assert!(sidecar.shutdown(Duration::from_secs(10)), "it was never blocked on a full pipe");
    }

    #[test]
    fn a_sidecar_that_ends_before_the_handshake_is_an_error_at_once() {
        let started = std::time::Instant::now();

        let error = Sidecar::spawn(powershell("exit 3"), &log_path("early"), Duration::from_secs(60))
            .err()
            .unwrap();

        assert!(error.contains("ended before"), "{error}");
        assert!(started.elapsed() < Duration::from_secs(30), "it did not wait for the timeout");
    }

    #[test]
    fn a_sidecar_that_never_says_it_is_ready_is_killed_after_the_timeout() {
        let started = std::time::Instant::now();

        let error = Sidecar::spawn(
            powershell("Start-Sleep -Seconds 60"),
            &log_path("silent"),
            Duration::from_secs(2),
        )
        .err()
        .unwrap();

        assert!(error.contains("did not say"), "{error}");
        assert!(started.elapsed() < Duration::from_secs(30), "it gave up at the timeout");
    }

    #[test]
    fn a_sidecar_that_ignores_stdin_is_killed_after_the_grace_period() {
        let script = format!("{READY} Start-Sleep -Seconds 60");
        let mut sidecar =
            Sidecar::spawn(powershell(&script), &log_path("stubborn"), Duration::from_secs(30)).unwrap();

        assert!(!sidecar.shutdown(Duration::from_millis(500)), "it had to be killed");

        assert!(sidecar.child.try_wait().unwrap().is_some(), "and it is gone");
    }

    #[test]
    fn what_the_sidecar_writes_to_stderr_is_kept_in_the_log_file() {
        let path = log_path("stderr");
        let script = format!("[Console]::Error.WriteLine('oops from the sidecar'); {READY} {WAIT_FOR_STDIN_TO_END}");
        let mut sidecar = Sidecar::spawn(powershell(&script), &path, Duration::from_secs(30)).unwrap();
        assert!(sidecar.shutdown(Duration::from_secs(10)));

        let deadline = std::time::Instant::now() + Duration::from_secs(10);
        let mut content = String::new();
        while std::time::Instant::now() < deadline {
            content = std::fs::read_to_string(&path).unwrap_or_default();
            if content.contains("oops from the sidecar") {
                break;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        assert!(content.contains("oops from the sidecar"), "{content}");
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn a_program_that_does_not_exist_is_an_error() {
        let error = Sidecar::spawn(
            Command::new("definitely-not-a-program-xgs"),
            &log_path("missing"),
            Duration::from_secs(5),
        )
        .err()
        .unwrap();

        assert!(error.contains("could not start"), "{error}");
    }

    /// Waits for [path] to hold something and returns it (the stand-in writes it just before it ends).
    fn read_when_written(path: &PathBuf) -> String {
        let deadline = std::time::Instant::now() + Duration::from_secs(10);
        while std::time::Instant::now() < deadline {
            let content = std::fs::read_to_string(path).unwrap_or_default();
            if !content.is_empty() {
                return content;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        String::new()
    }

    #[test]
    fn shutdown_tells_the_sidecar_that_the_user_asked_for_the_end() {
        let result = log_path("exit-line");
        let _ = std::fs::remove_file(&result);
        let script = format!(
            "{READY} $line = [Console]::In.ReadLine(); [IO.File]::WriteAllText('{}', [string]$line)",
            result.display()
        );
        let mut sidecar = Sidecar::spawn(powershell(&script), &log_path("exit-line-err"), Duration::from_secs(30)).unwrap();

        assert!(sidecar.shutdown(Duration::from_secs(10)));

        assert_eq!(read_when_written(&result), "exit");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn a_dropped_sidecar_closes_its_stdin_without_an_exit_line() {
        let result = log_path("dropped");
        let _ = std::fs::remove_file(&result);
        let script = format!(
            "{READY} $line = [Console]::In.ReadLine(); if ($null -eq $line) {{ $line = 'eof' }}; [IO.File]::WriteAllText('{}', [string]$line)",
            result.display()
        );
        let sidecar = Sidecar::spawn(powershell(&script), &log_path("dropped-err"), Duration::from_secs(30)).unwrap();

        drop(sidecar); // the shell is gone: the pipe closes and nobody wrote an exit line

        assert_eq!(read_when_written(&result), "eof");
        let _ = std::fs::remove_file(&result);
    }
}
