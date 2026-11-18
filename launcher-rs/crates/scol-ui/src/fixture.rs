// SPDX-License-Identifier: GPL-3.0-only
//! A synthetic install folder for the self test and the tests: an `sc-offline.ini`, three plugin
//! folders (one disabled, one with a `[settings]` block) and, with the `fixtures` feature, the
//! signed golden catalogue index. Invented names only. Nothing here was run in game.

use std::path::Path;

use scol_catalogue::trust::TrustRoot;

use crate::backend::Backend;

/// The TEST key and index are compiled in only with the `fixtures` feature. A release build of the
/// window never carries the test key, so it can never accept the golden index.
#[cfg(feature = "fixtures")]
const GOLDEN_INDEX: &[u8] = include_bytes!("../../../tests/catalogue/golden/index.json");
#[cfg(feature = "fixtures")]
const GOLDEN_SIG: &[u8] = include_bytes!("../../../tests/catalogue/golden/index.json.sig");
#[cfg(feature = "fixtures")]
const TEST_PUBLIC_HEX: &str = include_str!("../../../tests/catalogue/golden/TEST-ONLY-public.hex");

const PLUGIN_A: &str = "id = alpha\nname = Alpha Pack\nversion = 1.2\nauthor = Example Author\ndescription = Synthetic data plugin for the window test.\napi = 1.0\nkind = data\nentry = none\n";
const PLUGIN_B: &str = "id = beta\nname = Beta Script\nversion = 0.3\nauthor = Example Author\napi = 1.0\nkind = lua\nentry = main.lua\n[settings]\nenabled = bool default true label \"Enabled\" help \"Turn it on\"\ncount = int min 1 max 10 default 5\nmode = enum(fast,slow) default slow\n";
const PLUGIN_C: &str = "id = gamma\nname = Gamma Native\nversion = 2.0\nauthor = Example Author\napi = 1.0\nkind = native\nentry = gamma.dll\n";

fn put(p: &Path, text: &str) -> Result<(), String> {
    if let Some(d) = p.parent() {
        std::fs::create_dir_all(d).map_err(|e| format!("{}: {e}", d.display()))?;
    }
    std::fs::write(p, text).map_err(|e| format!("{}: {e}", p.display()))
}

/// Write the fixture install under `root` (created).
pub fn build(root: &Path) -> Result<(), String> {
    put(
        &root.join("sc-offline.ini"),
        "plugins = on\nlanguage = en\nclean_logs = ask\nexp_audio_native = off\n",
    )?;
    let plugins = root.join("data").join("plugins");
    put(&plugins.join("alpha").join("plugin.ini"), PLUGIN_A)?;
    put(&plugins.join("beta").join("plugin.ini"), PLUGIN_B)?;
    put(&plugins.join("beta").join("main.lua"), "-- synthetic\n")?;
    put(&plugins.join("beta").join("disabled"), "")?;
    put(&plugins.join("gamma").join("plugin.ini"), PLUGIN_C)?;
    #[cfg(feature = "fixtures")]
    {
        let cat = root.join("data").join("launcher").join("catalogue");
        std::fs::create_dir_all(&cat).map_err(|e| e.to_string())?;
        std::fs::write(cat.join("index.json"), GOLDEN_INDEX).map_err(|e| e.to_string())?;
        std::fs::write(cat.join("index.json.sig"), GOLDEN_SIG).map_err(|e| e.to_string())?;
    }
    Ok(())
}

/// A backend for the self test: trusts the TEST key (when the `fixtures` feature is on), runs
/// nothing and has no network.
pub struct FixtureBackend;

impl Backend for FixtureBackend {
    fn trust(&self) -> TrustRoot {
        #[cfg(feature = "fixtures")]
        if let Some(t) = TrustRoot::from_hex_keys(&[TEST_PUBLIC_HEX.trim()]) {
            return t;
        }
        TrustRoot::embedded()
    }
}
