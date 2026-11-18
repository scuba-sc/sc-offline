// SPDX-License-Identifier: GPL-3.0-only
//! `multiplayer_allow` ranges and the address list the StarCitizen.exe block rule uses when the
//! LAN stays open (`ParseAllowList`, `BlockedOutsideLan`, `Ipv4Text`).

/// First and last address of a range.
pub type Range = (u32, u32);

/// `ParseAllowList`: the ranges and the list rewritten from the numbers, or None when any item
/// is wrong. The validation is `scol_config::config::parse_allow_list`; the ranges are read back
/// from its rewritten text.
pub fn parse_allow_list(v: &str) -> Option<(Vec<Range>, String)> {
    let text = scol_config::config::parse_allow_list(v)?;
    let mut ranges = Vec::new();
    for item in text.split(',').filter(|s| !s.is_empty()) {
        let (addr, bits) = item.split_once('/')?;
        let mut ip: u32 = 0;
        for part in addr.split('.') {
            ip = (ip << 8) | part.parse::<u32>().ok()?;
        }
        let n: u32 = bits.parse().ok()?;
        let mask: u32 = if n == 32 {
            0xFFFF_FFFF
        } else {
            !(0xFFFF_FFFFu32 >> n)
        };
        ranges.push((ip, ip | !mask));
    }
    Some((ranges, text))
}

/// `Ipv4Text`.
pub fn ipv4_text(ip: u32) -> String {
    format!(
        "{}.{}.{}.{}",
        ip >> 24,
        (ip >> 16) & 255,
        (ip >> 8) & 255,
        ip & 255
    )
}

/// `BlockedOutsideLan`: every address except loopback, the private and link-local ranges and the
/// allowed ranges. IPv6 is blocked entirely.
pub fn blocked_outside_lan(allow: &[Range]) -> String {
    let mut keep: Vec<Range> = vec![
        (0x0A00_0000, 0x0AFF_FFFF),
        (0x7F00_0000, 0x7FFF_FFFF),
        (0xA9FE_0000, 0xA9FE_FFFF),
        (0xAC10_0000, 0xAC1F_FFFF),
        (0xC0A8_0000, 0xC0A8_FFFF),
    ];
    keep.extend_from_slice(allow);
    keep.sort_by_key(|r| r.0);
    let mut out = String::new();
    let mut next: u64 = 0;
    for &(first, last) in &keep {
        if u64::from(first) > next {
            if !out.is_empty() {
                out.push(',');
            }
            out.push_str(&format!(
                "{}-{}",
                ipv4_text(next as u32),
                ipv4_text(first - 1)
            ));
        }
        if u64::from(last) + 1 > next {
            next = u64::from(last) + 1;
        }
    }
    if next <= 0xFFFF_FFFF {
        if !out.is_empty() {
            out.push(',');
        }
        out.push_str(&format!("{}-255.255.255.255", ipv4_text(next as u32)));
    }
    out.push_str(",::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    out
}
