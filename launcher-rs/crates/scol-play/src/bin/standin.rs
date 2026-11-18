// SPDX-License-Identifier: GPL-3.0-only
//! Stand-in for StarCitizen.exe in tests. It sleeps for `SCOL_STANDIN_MS` milliseconds (default 300)
//! and exits with `SCOL_STANDIN_EXIT` (default 0). It touches nothing and opens no window.

fn main() {
    let ms: u64 = std::env::var("SCOL_STANDIN_MS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(300);
    let code: i32 = std::env::var("SCOL_STANDIN_EXIT")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(0);
    std::thread::sleep(std::time::Duration::from_millis(ms));
    std::process::exit(code);
}
