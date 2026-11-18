// SPDX-License-Identifier: GPL-3.0-only
//! The launcher's version text. `src/version.h` is the source; the unit test below fails when the two
//! drift apart.

/// `SCO_VERSION` of `src/version.h`: one version for the DLL and the launcher.
pub const SCO_VERSION: &str = "0.7.8.3";
/// `SCO_BASED_ON` of `src/version.h`.
pub const SCO_BASED_ON: &str = "based on ChrisWareOffline 0.9.0-rc1";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_version_h() {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../src/version.h");
        let Ok(text) = std::fs::read_to_string(&path) else {
            return; // the crate was copied out of the repository: nothing to compare with
        };
        let define = |name: &str| {
            let line = text
                .lines()
                .find(|l| l.starts_with(&format!("#define {name} ")))
                .unwrap_or_else(|| panic!("{name} missing in version.h"));
            line.splitn(3, ' ')
                .nth(2)
                .unwrap()
                .trim()
                .trim_matches('"')
                .to_string()
        };
        assert_eq!(define("SCO_VERSION"), SCO_VERSION);
        assert_eq!(define("SCO_BASED_ON"), SCO_BASED_ON);
    }
}
