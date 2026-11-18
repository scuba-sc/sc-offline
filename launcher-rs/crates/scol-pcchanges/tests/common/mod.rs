// SPDX-License-Identifier: GPL-3.0-only
//! Shared fixture. Made-up folders and names only. Nothing here touches the machine.

#![allow(dead_code)]

use scol_pcchanges::fake::FakeSystem;
use scol_pcchanges::system::LocalTime;

pub const RECORD: &str = r"C:\ProgramData\sc-offline\pc-changes.txt";
pub const RECORD_DIR: &str = r"C:\ProgramData\sc-offline";
pub const HOSTS: &str = r"C:\Windows\System32\drivers\etc\hosts";
pub const EAC: &str = r"C:\Program Files (x86)\EasyAntiCheat_EOS\EasyAntiCheat_EOS.exe";
pub const EAC_BAK: &str = r"C:\Program Files (x86)\EasyAntiCheat_EOS\EasyAntiCheat_EOS.exe.bak";
pub const BIN: &str = r"C:\Games\StarCitizen\LIVE\Bin64";
pub const GAME: &str = r"C:\Games\StarCitizen\LIVE\Bin64\StarCitizen.exe";
pub const CRASH: &str = r"C:\Games\StarCitizen\LIVE\Tools\Public\CrashHandler.exe";
pub const RSI: &str = r"C:\Games\RSI Launcher\RSI Launcher.exe";

pub const TAG: &str = "# added by sc-offline, removed when the game closes";
pub const EAC_HOST: &str = "modules-cdn.eac-prod.on.epicgames.com";

/// `BlockedOutsideLan` with no allow list.
pub const REMOTE_DEFAULT: &str = "0.0.0.0-9.255.255.255,11.0.0.0-126.255.255.255,128.0.0.0-169.253.255.255,169.255.0.0-172.15.255.255,172.32.0.0-192.167.255.255,192.169.0.0-255.255.255.255,::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff";

pub const NETSH: &str = r"C:\Windows\System32\netsh.exe";
pub const IPCONFIG: &str = r"C:\Windows\System32\ipconfig.exe";

/// A machine with the game, the crash handler, the RSI Launcher beside the library, EAC
/// installed and a plain hosts file. Time is 2026-11-17 09:05.
pub fn machine() -> FakeSystem {
    let mut s = FakeSystem::new();
    s.put(GAME, "x");
    s.put(CRASH, "x");
    s.put(RSI, "x");
    s.put(EAC, "x");
    s.put(HOSTS, "127.0.0.1 localhost\r\n");
    s.time = LocalTime {
        year: 2026,
        month: 11,
        day: 17,
        hour: 9,
        minute: 5,
    };
    s
}

/// The log line the fake writes for a netsh call.
pub fn netsh(args: &str) -> String {
    format!("run \"{NETSH}\" {args}")
}

pub fn block(rule: &str, dir: &str, exe: &str, remote: &str) -> String {
    let tail = if remote.is_empty() {
        String::new()
    } else {
        format!(" remoteip={remote}")
    };
    netsh(&format!(
        "advfirewall firewall add rule name=\"{rule}\" dir={dir} action=block enable=yes profile=any program=\"{exe}\"{tail}"
    ))
}

pub fn delete_rule(rule: &str) -> String {
    netsh(&format!("advfirewall firewall delete rule name=\"{rule}\""))
}

pub fn show_rule(rule: &str) -> String {
    netsh(&format!("advfirewall firewall show rule name=\"{rule}\""))
}

pub fn flush() -> String {
    format!("run \"{IPCONFIG}\" /flushdns")
}

pub const R_GAME: &str = "sc-offline: block StarCitizen.exe";
pub const R_RSI: &str = "sc-offline: block RSI Launcher.exe";
pub const R_CRASH: &str = "sc-offline: block CrashHandler.exe";
pub const R_MP: &str = "sc-offline: allow StarCitizen.exe multiplayer (LAN)";

pub fn all_wanted() -> scol_pcchanges::PcWanted {
    scol_pcchanges::PcWanted {
        firewall: true,
        eac_hosts: true,
        eac_rename: true,
        lan: true,
        allow: String::new(),
    }
}

pub fn text(out: Vec<u8>) -> String {
    String::from_utf8(out).unwrap()
}
