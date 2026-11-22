// SPDX-License-Identifier: GPL-3.0-only
//! The reader the window puts on its child's output. A question the child asks has no line end (the
//! answer is typed after it), so it must reach the window while the child is still waiting, not when
//! the child ends. Synthetic input only; no process is started and nothing is run in game.
#![cfg(windows)]

use scol_cli::window::read_lines;
use std::io::Read;
use std::sync::mpsc;
use std::time::Duration;

/// Bytes arrive in the chunks the test sends; the end of the channel is the end of the input.
struct Chunks(mpsc::Receiver<Vec<u8>>, Vec<u8>);

impl Read for Chunks {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        if self.1.is_empty() {
            match self.0.recv() {
                Ok(c) => self.1 = c,
                Err(_) => return Ok(0),
            }
        }
        let n = buf.len().min(self.1.len());
        buf[..n].copy_from_slice(&self.1[..n]);
        self.1.drain(..n);
        Ok(n)
    }
}

/// The whole path with a real child process: `cmd.exe` asks a question without a line end, the window's
/// backend hands it over while the child waits, and the answer reaches the child through its input.
#[test]
fn a_child_process_is_answered_through_its_input() {
    use scol_cli::window::WindowBackend;
    use scol_ui::Backend;
    use std::sync::Arc;

    let system = std::env::var("SystemRoot").unwrap_or_else(|_| "C:\\Windows".into());
    let cmd = std::path::PathBuf::from(system).join("System32\\cmd.exe");
    let backend = Arc::new(WindowBackend::new());
    let (tx, lines) = mpsc::channel::<String>();
    let runner = {
        let backend = backend.clone();
        std::thread::spawn(move || {
            backend.run_child(
                &cmd,
                &[
                    "/d",
                    "/v:on",
                    "/c",
                    "set /p A=Delete these files? [y/N/always/never] & echo answered !A!",
                ],
                &mut |l| {
                    let _ = tx.send(l.to_string());
                },
            )
        })
    };
    let question = lines
        .recv_timeout(Duration::from_secs(20))
        .expect("the question arrives while cmd.exe waits");
    assert_eq!(question, "Delete these files? [y/N/always/never] ");
    backend.answer("always");
    let answered = lines
        .recv_timeout(Duration::from_secs(20))
        .expect("cmd.exe read the answer");
    assert_eq!(answered.trim_end(), "answered always");
    assert_eq!(runner.join().unwrap(), Ok(0));
    backend.answer("y"); // no child runs: nothing to write to, and no panic
}

#[test]
fn a_question_reaches_the_window_while_the_child_still_waits() {
    let (feed, source) = mpsc::channel::<Vec<u8>>();
    let (tx, lines) = mpsc::channel::<String>();
    let reader = std::thread::spawn(move || read_lines(Chunks(source, Vec::new()), &tx));
    let next = || {
        lines
            .recv_timeout(Duration::from_secs(10))
            .expect("a line arrives while the input is still open")
    };

    feed.send(b"Logs:     kept\r\nhalf a ".to_vec()).unwrap();
    assert_eq!(next(), "Logs:     kept");
    // The rest of the line and a question split over two reads.
    feed.send(b"line\nDelete these files? [y/N/alw".to_vec())
        .unwrap();
    assert_eq!(next(), "half a line");
    feed.send(b"ays/never] ".to_vec()).unwrap();
    assert_eq!(next(), "Delete these files? [y/N/always/never] ");
    // What the child prints after the answer starts a line of its own in the window.
    feed.send(b"Logs:     deleted 1 file\nMake a crash report? [y/N] ".to_vec())
        .unwrap();
    assert_eq!(next(), "Logs:     deleted 1 file");
    assert_eq!(next(), "Make a crash report? [y/N] ");
    // Text without a line end that is no question waits for the end of the input.
    feed.send(b"no line end".to_vec()).unwrap();
    assert!(lines.recv_timeout(Duration::from_millis(200)).is_err());
    drop(feed);
    assert_eq!(next(), "no line end");
    reader.join().unwrap();
    assert!(lines.recv().is_err(), "nothing after the end");
}
