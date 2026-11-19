// SPDX-License-Identifier: GPL-3.0-only
//! The GitHub release reply: which release is on offer on the player's channel, and its zip asset
//! with the SHA-256 digest GitHub publishes.

use serde_json::Value;

use crate::version;

/// The release API, hard-coded like the C++ launcher.
pub const RELEASES_LATEST: &str =
    "https://api.github.com/repos/scuba-sc/sc-offline/releases/latest";
pub const RELEASES_LIST: &str =
    "https://api.github.com/repos/scuba-sc/sc-offline/releases?per_page=20";
/// Where a player updates by hand.
pub const MANUAL_URL: &str = "https://github.com/scuba-sc/sc-offline/releases/latest";
/// Hosts the first request and the asset URL may use.
pub const HOSTS: [&str; 2] = ["api.github.com", "github.com"];
/// Hosts a redirect may go to (GitHub's signed asset links).
pub const REDIRECT_HOSTS: [&str; 3] = [
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
    "github-releases.githubusercontent.com",
];
/// The release reply cap.
pub const MAX_REPLY_BYTES: u64 = 4 << 20;
/// The zip cap when GitHub gives no size.
pub const MAX_ZIP_BYTES: u64 = 256 << 20;
const ASSET_PREFIX: &str = "/scuba-sc/sc-offline/releases/download/";

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct UpdateInfo {
    pub tag: String,
    pub zip_name: String,
    pub zip_url: String,
    pub sha256: String,
    pub size: u64,
}

/// A tag that is safe to put in a path: a parseable version, `v` first.
pub fn tag_ok(tag: &str) -> bool {
    tag.starts_with('v') && version::parse(tag).is_some()
}

fn asset_of(rel: &Value, tag: &str) -> Result<UpdateInfo, String> {
    let want = format!("sc-offline-{tag}.zip");
    let assets = rel
        .get("assets")
        .and_then(|a| a.as_array())
        .ok_or_else(|| format!("{tag} has no assets"))?;
    for a in assets {
        let s = |k: &str| a.get(k).and_then(|x| x.as_str());
        if s("name") != Some(want.as_str()) {
            continue;
        }
        let (Some(url), Some(digest)) = (s("browser_download_url"), s("digest")) else {
            continue;
        };
        let Some(hex) = digest.strip_prefix("sha256:") else {
            continue;
        };
        let hex = hex.to_ascii_lowercase();
        if hex.len() != 64 || !hex.bytes().all(|c| c.is_ascii_hexdigit()) {
            continue;
        }
        // Hardening over the C++ launcher: the asset must live under this project's release path.
        let on_project = scol_catalogue::fetch::check_url(url, &HOSTS).is_ok()
            && url
                .strip_prefix("https://")
                .and_then(|r| r.split_once('/'))
                .is_some_and(|(_, p)| format!("/{p}").starts_with(ASSET_PREFIX));
        if !on_project {
            continue;
        }
        let size = a.get("size").and_then(|x| x.as_u64()).unwrap_or(0);
        return Ok(UpdateInfo {
            tag: tag.to_string(),
            zip_name: want,
            zip_url: url.to_string(),
            sha256: hex,
            size,
        });
    }
    Err(format!("{tag} has no {want} with a SHA-256 digest"))
}

/// S5: what GitHub offers on the player's channel. `body` is one release object (stable, the
/// "latest" reply) or an array of them (prerelease). Drafts never; pre-releases only on the
/// prerelease channel. Err is the reason nothing is offered, worded like the C++ launcher
/// ("up to date (latest v1.2.3)" starts with "up to date").
pub fn pick(body: &[u8], current: &str, prerelease: bool) -> Result<UpdateInfo, String> {
    let v: Value =
        serde_json::from_slice(body).map_err(|_| "unexpected reply from GitHub".to_string())?;
    let tops: Vec<&Value> = match &v {
        Value::Object(_) => vec![&v],
        Value::Array(a) => a.iter().filter(|x| x.is_object()).collect(),
        _ => return Err("unexpected reply from GitHub".into()),
    };
    let mut best: Option<UpdateInfo> = None;
    let mut why = String::new();
    let mut newest_seen = String::new();
    for rel in &tops {
        let Some(tag) = rel.get("tag_name").and_then(|t| t.as_str()) else {
            continue;
        };
        let flag = |k: &str| rel.get(k).and_then(|x| x.as_bool()).unwrap_or(false);
        if flag("draft") || (!prerelease && flag("prerelease")) {
            if tops.len() == 1 {
                why = "latest release is a pre-release".into();
            }
            continue;
        }
        if newest_seen.is_empty() || version::greater(tag, &newest_seen) {
            newest_seen = tag.to_string();
        }
        if !version::greater(tag, current) || !tag_ok(tag) {
            continue;
        }
        if let Some(b) = &best {
            if !version::greater(tag, &b.tag) {
                continue;
            }
        }
        match asset_of(rel, tag) {
            Ok(u) => best = Some(u),
            Err(w) => why = w,
        }
    }
    if let Some(u) = best {
        return Ok(u);
    }
    Err(if !why.is_empty() {
        why
    } else if newest_seen.is_empty() {
        "no releases found".into()
    } else {
        format!("up to date (latest {newest_seen})")
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rel(tag: &str, pre: bool, with_asset: bool) -> String {
        let assets = if with_asset {
            format!(
                r#"[{{"name":"sc-offline-{tag}.zip","size":10,"digest":"sha256:{}","browser_download_url":"https://github.com/scuba-sc/sc-offline/releases/download/{tag}/sc-offline-{tag}.zip"}}]"#,
                "A".repeat(64)
            )
        } else {
            "[]".into()
        };
        format!(r#"{{"tag_name":"{tag}","prerelease":{pre},"draft":false,"assets":{assets}}}"#)
    }

    #[test]
    fn stable_offer() {
        let u = pick(rel("v0.7.1", false, true).as_bytes(), "0.7.0", false).unwrap();
        assert_eq!(u.tag, "v0.7.1");
        assert_eq!(u.sha256, "a".repeat(64));
        assert_eq!(u.size, 10);
    }

    #[test]
    fn reasons() {
        let up = pick(rel("v0.7.0", false, true).as_bytes(), "0.7.0", false).unwrap_err();
        assert!(up.starts_with("up to date"));
        assert!(pick(rel("v0.7.1", true, true).as_bytes(), "0.7.0", false)
            .unwrap_err()
            .contains("pre-release"));
        assert!(pick(rel("v0.7.1", false, false).as_bytes(), "0.7.0", false)
            .unwrap_err()
            .contains("no sc-offline-v0.7.1.zip"));
        assert!(pick(b"junk", "0.7.0", false).is_err());
    }

    #[test]
    fn prerelease_channel_takes_newest() {
        let body = format!(
            "[{},{},{}]",
            rel("v0.7.1", false, true),
            rel("v0.8.0-rc1", true, true),
            rel("v0.7.5", false, true)
        );
        assert_eq!(
            pick(body.as_bytes(), "0.7.0", true).unwrap().tag,
            "v0.8.0-rc1"
        );
        assert_eq!(pick(body.as_bytes(), "0.7.0", false).unwrap().tag, "v0.7.5");
    }

    /// The release-candidate rules, the same cases as `tools/launcher_update_version_test.cpp` in the C++ launcher.
    #[test]
    fn release_candidate_channels() {
        let rc1 = rel("v0.8.0-rc.1", true, true);
        let rc2 = rel("v0.8.0-rc.2", true, true);
        let stable = rel("v0.8.0", false, true);
        let old = rel("v0.7.7", false, true);
        let list = |r: &[&str]| format!("[{}]", r.join(","));
        let offer = |body: &str, pre: bool, current: &str| {
            pick(body.as_bytes(), current, pre).map(|u| u.tag).ok()
        };
        // A 0.8.0-rc.1 launcher on the stable channel is offered 0.8.0 once it is published ...
        assert_eq!(
            offer(&stable, false, "0.8.0-rc.1").as_deref(),
            Some("v0.8.0")
        );
        assert_eq!(
            offer(&list(&[&stable, &rc2, &rc1, &old]), false, "0.8.0-rc.1").as_deref(),
            Some("v0.8.0")
        );
        // ... and not another pre-release.
        assert_eq!(offer(&rc2, false, "0.8.0-rc.1"), None);
        assert_eq!(offer(&list(&[&rc2, &rc1, &old]), false, "0.8.0-rc.1"), None);
        // On the pre-release channel the newest is offered.
        assert_eq!(
            offer(&list(&[&rc2, &rc1, &old]), true, "0.8.0-rc.1").as_deref(),
            Some("v0.8.0-rc.2")
        );
        assert_eq!(
            offer(&list(&[&stable, &rc2, &rc1]), true, "0.8.0-rc.1").as_deref(),
            Some("v0.8.0")
        );
        assert_eq!(offer(&list(&[&rc1, &old]), true, "0.8.0-rc.1"), None);
        assert!(pick(list(&[&rc1, &old]).as_bytes(), "0.8.0-rc.1", true)
            .unwrap_err()
            .starts_with("up to date"));
        // A 0.7.x launcher: no pre-release on the stable channel, whatever else is published.
        assert_eq!(offer(&rc1, false, "0.7.7"), None);
        assert_eq!(offer(&list(&[&rc1, &old]), false, "0.7.7"), None);
        let v078 = rel("v0.7.8", false, true);
        assert_eq!(
            offer(&list(&[&rc2, &rc1, &v078, &old]), false, "0.7.7").as_deref(),
            Some("v0.7.8")
        );
        assert_eq!(offer(&stable, false, "0.7.7").as_deref(), Some("v0.8.0"));
        assert_eq!(
            offer(&list(&[&rc1, &old]), true, "0.7.7").as_deref(),
            Some("v0.8.0-rc.1")
        );
        assert_eq!(offer(&rc1, true, "0.7.8").as_deref(), Some("v0.8.0-rc.1"));
        // A release without the zip that carries its own tag is skipped.
        let bad = rc1.replace("sc-offline-v0.8.0-rc.1.zip\"", "sc-offline-v0.8.0.zip\"");
        assert_eq!(offer(&bad, true, "0.7.7"), None);
    }

    #[test]
    fn foreign_asset_url_is_ignored() {
        let body = rel("v0.7.1", false, true).replace("scuba-sc/sc-offline", "evil/sc-offline");
        assert!(pick(body.as_bytes(), "0.7.0", false).is_err());
        let body = rel("v0.7.1", false, true).replace("https://github.com", "https://evil.example");
        assert!(pick(body.as_bytes(), "0.7.0", false).is_err());
    }
}
