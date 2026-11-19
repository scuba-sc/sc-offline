// SPDX-License-Identifier: GPL-3.0-only
//! en and ru text. The table is generated (`tools/gen_locale.py`) from the C++ launcher's locale
//! source, so the two launchers say the same thing. A key missing in Russian falls back to English;
//! a key missing in both shows the key.

use crate::locale_table::{EN, RU};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Lang {
    En,
    Ru,
}

fn find(table: &'static [(&'static str, &'static str)], key: &str) -> Option<&'static str> {
    table
        .binary_search_by(|(k, _)| (*k).cmp(key))
        .ok()
        .map(|i| table[i].1)
}

/// Text for `key`.
pub fn tr(lang: Lang, key: &str) -> String {
    let hit = match lang {
        Lang::Ru => find(RU, key).or_else(|| find(EN, key)),
        Lang::En => find(EN, key),
    };
    hit.map(str::to_string).unwrap_or_else(|| key.to_string())
}

/// Text for `key` with each `{name}` replaced.
pub fn trf(lang: Lang, key: &str, args: &[(&str, &str)]) -> String {
    let mut s = tr(lang, key);
    for (name, value) in args {
        s = s.replace(&format!("{{{name}}}"), value);
    }
    s
}

/// The language the window starts in: `language =` of the ini, and for auto the Windows UI language
/// (`LANG`/`LANGUAGE` style variables are read too, which is what Wine sets).
pub fn resolve(config: scol_config::Language) -> Lang {
    match config {
        scol_config::Language::English => Lang::En,
        scol_config::Language::Russian => Lang::Ru,
        scol_config::Language::Auto => {
            let is_ru = ["LC_ALL", "LANGUAGE", "LANG"]
                .iter()
                .filter_map(|v| std::env::var(v).ok())
                .any(|v| v.to_ascii_lowercase().starts_with("ru"));
            if is_ru {
                Lang::Ru
            } else {
                Lang::En
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tables_are_sorted_and_share_keys() {
        for t in [EN, RU] {
            assert!(t.windows(2).all(|w| w[0].0 < w[1].0));
        }
        // Every English key has a Russian line (the C++ catalogue check enforces the same).
        for (k, _) in EN {
            assert!(find(RU, k).is_some(), "no Russian text for {k}");
        }
    }

    #[test]
    fn placeholders_match_between_languages() {
        let holes = |s: &str| {
            let mut v: Vec<String> = s
                .split('{')
                .skip(1)
                .filter_map(|p| p.split('}').next().map(str::to_string))
                .collect();
            v.sort();
            v
        };
        for (k, en) in EN {
            let ru = find(RU, k).unwrap();
            assert_eq!(holes(en), holes(ru), "placeholders differ for {k}");
        }
    }

    /// Every text key the code names exists in English and in Russian, so a page never shows a key.
    #[test]
    fn no_missing_keys_in_code() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("src");
        let mut seen = 0;
        for e in std::fs::read_dir(dir).unwrap().flatten() {
            let path = e.path();
            if path.extension().is_none_or(|x| x != "rs")
                || path.file_name().is_some_and(|n| n == "locale_table.rs")
            {
                continue;
            }
            let text = std::fs::read_to_string(&path).unwrap();
            let mut rest = text.as_str();
            while let Some(i) = rest.find("\"launcher.") {
                let tail = &rest[i + 1..];
                let end = tail.find('"').unwrap();
                let key = &tail[..end];
                rest = &tail[end + 1..];
                // Keys with a hole (`launcher.ui.cap.{}.name`) are built at run time; caps.rs tests them.
                if key.contains('{') || key.ends_with('.') || key.contains(' ') {
                    continue;
                }
                seen += 1;
                assert!(
                    find(EN, key).is_some(),
                    "{}: no English text for {key}",
                    path.display()
                );
                assert!(
                    find(RU, key).is_some(),
                    "{}: no Russian text for {key}",
                    path.display()
                );
            }
        }
        assert!(seen > 80, "the scan found only {seen} keys");
    }

    #[test]
    fn lookup_and_format() {
        assert_eq!(tr(Lang::En, "launcher.action.play"), "Play");
        assert_eq!(tr(Lang::Ru, "launcher.action.play"), "Играть");
        assert_eq!(
            trf(
                Lang::En,
                "launcher.status.plugins_count",
                &[("enabled", "2"), ("total", "5")]
            ),
            "2 of 5 enabled"
        );
        assert_eq!(tr(Lang::En, "no.such.key"), "no.such.key");
        assert!(tr(Lang::En, "launcher.plugins.disabled_error").contains("data\\plugins\\"));
    }
}
