// SPDX-License-Identifier: GPL-3.0-only
//! The `mcp` command entry (`run_with`, and on Windows `run_in` over a scratch install folder). Synthetic data
//! only; nothing here runs in game.

use std::io::Cursor;

use scol_mcp::json::parse;
use scol_mcp::{run_with, Io, Platform, PluginFolder};

struct Mini {
    game_log: bool,
}

impl Platform for Mini {
    fn version(&self) -> String {
        "1.2.3-test".to_string()
    }
    fn game_log(&self) -> bool {
        self.game_log
    }
    fn plugins(&mut self) -> Vec<PluginFolder> {
        vec![PluginFolder {
            folder: b"demo".to_vec(),
            ini_text: b"id = demo\nname = Demo\nversion = 1\n".to_vec(),
            disabled: false,
        }]
    }
}

struct Outcome {
    code: i32,
    stdout: String,
    stderr: String,
    game_log: Option<bool>,
}

fn run(args: &[&str], input: &str) -> Outcome {
    let args: Vec<String> = args.iter().map(|a| a.to_string()).collect();
    let mut stdin = Cursor::new(input.as_bytes().to_vec());
    let (mut stdout, mut stderr) = (Vec::new(), Vec::new());
    let mut seen = None;
    let code = {
        let mut io = Io {
            stdin: &mut stdin,
            stdout: &mut stdout,
            stderr: &mut stderr,
        };
        run_with(&args, &mut io, &mut |game_log| {
            seen = Some(game_log);
            Box::new(Mini { game_log })
        })
    };
    Outcome {
        code,
        stdout: String::from_utf8(stdout).unwrap(),
        stderr: String::from_utf8(stderr).unwrap(),
        game_log: seen,
    }
}

const PING: &str = r#"{"jsonrpc":"2.0","id":1,"method":"ping"}"#;

#[test]
fn answers_one_line_per_request_and_keeps_stdout_for_the_protocol() {
    let o = run(
        &[],
        &format!(
            "{PING}\n{{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}}\n{PING}\n"
        ),
    );
    assert_eq!(o.code, 0);
    assert_eq!(o.stdout, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n");
    assert_eq!(
        o.stderr,
        "sc-offline mcp: read-only server on stdio, 1.2.3-test\n"
    );
    assert_eq!(o.game_log, Some(false));
}

#[test]
fn a_tool_call_runs_through_the_platform() {
    let call = r#"{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"list_plugins"}}"#;
    let o = run(&[], &format!("{call}\n"));
    let reply = parse(o.stdout.trim_end().as_bytes());
    assert!(reply.ok);
    let structured = reply
        .value
        .find(b"result")
        .unwrap()
        .find(b"structuredContent")
        .unwrap();
    assert_eq!(structured.find(b"count").unwrap().as_bytes(), b"1");
    assert_eq!(
        structured.find(b"plugins").unwrap().items()[0]
            .find(b"folder")
            .unwrap()
            .as_bytes(),
        b"demo"
    );
}

#[test]
fn game_log_flag_reaches_the_platform_and_other_flags_are_accepted() {
    let o = run(
        &["--game-log", "--dry-run", "--console", "--skip-eac-check"],
        "",
    );
    assert_eq!(o.code, 0);
    assert_eq!(o.game_log, Some(true));
    assert_eq!(o.stdout, "");
}

#[test]
fn game_folder_is_ignored_with_a_note() {
    let o = run(&["--game", "D:\\Games\\SC"], "");
    assert_eq!(o.code, 0);
    assert!(o
        .stderr
        .starts_with("sc-offline mcp: --game is ignored; set game = in sc-offline.ini\n"));
    assert!(o.stderr.contains("read-only server on stdio"));
}

#[test]
fn bad_arguments_stop_before_the_server_starts() {
    let o = run(&["--game"], "");
    assert_eq!(o.code, 1);
    assert!(o.stderr.contains("--game needs a folder"));
    assert_eq!(o.game_log, None);

    let o = run(&["--bogus"], &format!("{PING}\n"));
    assert_eq!(o.code, 1);
    assert_eq!(o.stdout, "");
    assert!(o.stderr.starts_with("usage: sc-offline.exe"));
    assert!(o.stderr.ends_with("[!] unknown argument '--bogus'\n"));
    assert_eq!(o.game_log, None);

    let o = run(&["play"], "");
    assert_eq!(o.code, 1);
    assert!(o.stderr.ends_with("[!] unknown argument 'play'\n"));
}

#[test]
fn help_prints_the_usage_on_stdout() {
    for flag in ["--help", "-h", "/?"] {
        let o = run(&[flag], "");
        assert_eq!(o.code, 0, "{flag}");
        assert!(
            o.stdout.starts_with("usage: sc-offline.exe [command]"),
            "{flag}"
        );
        assert!(o.stdout.contains("--game-log"), "{flag}");
        assert_eq!(o.game_log, None);
    }
    let o = run(&["--help", "--game-log"], "");
    assert_eq!(o.code, 1);
    assert_eq!(o.stdout, "");
}

#[test]
fn a_client_that_went_away_stops_the_loop() {
    struct Broken;
    impl std::io::Write for Broken {
        fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
            Err(std::io::Error::new(std::io::ErrorKind::BrokenPipe, "gone"))
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    let mut stdin = Cursor::new(format!("{PING}\n{PING}\n").into_bytes());
    let mut stderr = Vec::new();
    let mut out = Broken;
    let mut io = Io {
        stdin: &mut stdin,
        stdout: &mut out,
        stderr: &mut stderr,
    };
    let code = run_with(&[], &mut io, &mut |g| Box::new(Mini { game_log: g }));
    assert_eq!(code, 0);
    // the first reply failed to write, so the second request was never read
    assert!(stdin.position() > 0);
}

/// The real install reader over a scratch folder: plugins, ini and logs are read, nothing is written.
#[cfg(windows)]
mod host {
    use super::*;
    use std::path::{Path, PathBuf};

    struct Scratch(PathBuf);
    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn tree(path: &Path) -> Vec<(String, u64)> {
        let mut out = Vec::new();
        fn walk(dir: &Path, root: &Path, out: &mut Vec<(String, u64)>) {
            for e in std::fs::read_dir(dir).unwrap().flatten() {
                let p = e.path();
                let rel = p.strip_prefix(root).unwrap().to_string_lossy().into_owned();
                if p.is_dir() {
                    out.push((rel, 0));
                    walk(&p, root, out);
                } else {
                    out.push((rel, std::fs::metadata(&p).unwrap().len()));
                }
            }
        }
        walk(path, path, &mut out);
        out.sort();
        out
    }

    fn call(id: u32, tool: &str, args: &str) -> String {
        format!("{{\"jsonrpc\":\"2.0\",\"id\":{id},\"method\":\"tools/call\",\"params\":{{\"name\":\"{tool}\",\"arguments\":{args}}}}}\n")
    }

    #[test]
    fn reads_plugins_settings_and_logs_and_writes_nothing() {
        let dir = std::env::temp_dir().join(format!("scol-mcp-host-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let scratch = Scratch(dir.clone());
        let data = dir.join("data");
        std::fs::create_dir_all(data.join("plugins").join("beta")).unwrap();
        std::fs::create_dir_all(data.join("plugins").join("Alpha")).unwrap();
        std::fs::create_dir_all(data.join("plugins").join("noini")).unwrap();
        std::fs::write(
            data.join("plugins").join("beta").join("plugin.ini"),
            "id = beta\nname = Beta\nversion = 2\n",
        )
        .unwrap();
        std::fs::write(data.join("plugins").join("beta").join("disabled"), "").unwrap();
        std::fs::write(
            data.join("plugins").join("Alpha").join("plugin.ini"),
            "id = Alpha\nname = Alpha\nversion = 1\nauthor = Test\n",
        )
        .unwrap();
        std::fs::write(
            dir.join("sc-offline.ini"),
            "# c\nchannel = LIVE\n[extra]\nkey = v\n",
        )
        .unwrap();
        std::fs::write(
            data.join("mod.log"),
            "first line\nsecond line\nthird line\n",
        )
        .unwrap();
        std::fs::write(data.join("launcher.log"), "launcher says hi\n").unwrap();
        let before = tree(&dir);

        let input = [
            call(1, "list_plugins", "{}"),
            call(2, "get_settings", "{}"),
            call(3, "read_log", r#"{"log":"mod","lines":2}"#),
            call(4, "read_log", r#"{"log":"launcher_previous"}"#),
            call(5, "read_log", r#"{"log":"game"}"#),
        ]
        .concat();
        let mut stdin = Cursor::new(input.into_bytes());
        let (mut stdout, mut stderr) = (Vec::new(), Vec::new());
        let code = {
            let mut io = Io {
                stdin: &mut stdin,
                stdout: &mut stdout,
                stderr: &mut stderr,
            };
            scol_mcp::run_in(&dir, &[], &mut io)
        };
        assert_eq!(code, 0);
        let out = String::from_utf8(stdout).unwrap();
        let replies: Vec<_> = out.lines().map(|l| parse(l.as_bytes()).value).collect();
        assert_eq!(replies.len(), 5);
        let result = |i: usize| replies[i].find(b"result").unwrap();
        let structured = |i: usize| result(i).find(b"structuredContent").unwrap();

        let plugins = structured(0).find(b"plugins").unwrap().items();
        let folders: Vec<&[u8]> = plugins
            .iter()
            .map(|p| p.find(b"folder").unwrap().as_bytes())
            .collect();
        assert_eq!(folders, [&b"Alpha"[..], b"beta"]);
        assert!(plugins[0].find(b"enabled").unwrap().as_bool());
        assert!(!plugins[1].find(b"enabled").unwrap().as_bool());
        assert_eq!(plugins[0].find(b"author").unwrap().as_bytes(), b"Test");

        let settings = structured(1).find(b"settings").unwrap().items();
        assert_eq!(settings.len(), 2);
        assert_eq!(settings[1].find(b"section").unwrap().as_bytes(), b"extra");
        assert!(structured(1).find(b"exists").unwrap().as_bool());

        let lines: Vec<&[u8]> = structured(2)
            .find(b"lines")
            .unwrap()
            .items()
            .iter()
            .map(|l| l.as_bytes())
            .collect();
        assert_eq!(lines, [&b"second line"[..], b"third line"]);
        assert_eq!(
            structured(2).find(b"matched_lines").unwrap().as_bytes(),
            b"3"
        );

        assert!(!structured(3).find(b"exists").unwrap().as_bool());
        // Game.log is not served without --game-log
        assert!(result(4).find(b"isError").unwrap().as_bool());
        assert!(String::from_utf8_lossy(
            result(4).find(b"content").unwrap().items()[0]
                .find(b"text")
                .unwrap()
                .as_bytes()
        )
        .starts_with("Game.log is not enabled."));
        assert_eq!(tree(&dir), before, "the server changed the install folder");
        drop(scratch);
    }

    #[test]
    fn junction_plugin_folders_are_listed() {
        let dir = std::env::temp_dir().join(format!("scol-mcp-junction-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let _scratch = Scratch(dir.clone());
        let plugins = dir.join("data").join("plugins");
        let real = dir.join("elsewhere");
        std::fs::create_dir_all(&plugins).unwrap();
        std::fs::create_dir_all(&real).unwrap();
        std::fs::write(
            real.join("plugin.ini"),
            "id = linked
name = Linked
",
        )
        .unwrap();
        let link = plugins.join("linked");
        let made = std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(&link)
            .arg(&real)
            .output()
            .map(|o| o.status.success())
            .unwrap_or(false);
        if !made {
            eprintln!("junction could not be created here; skipping");
            return;
        }
        let input = call(1, "list_plugins", "{}");
        let mut stdin = Cursor::new(input.into_bytes());
        let (mut stdout, mut stderr) = (Vec::new(), Vec::new());
        {
            let mut io = Io {
                stdin: &mut stdin,
                stdout: &mut stdout,
                stderr: &mut stderr,
            };
            assert_eq!(scol_mcp::run_in(&dir, &[], &mut io), 0);
        }
        let out = String::from_utf8(stdout).unwrap();
        assert!(out.contains("\"folder\":\"linked\""), "{out}");
        // remove the junction itself, not the target contents
        let _ = std::fs::remove_dir(&link);
    }
}
