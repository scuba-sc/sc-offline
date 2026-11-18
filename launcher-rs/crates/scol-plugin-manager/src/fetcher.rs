// SPDX-License-Identifier: GPL-3.0-only
//! A [`Fetcher`] that serves files from a local folder, for tests and offline use (a mirror
//! copied to a stick, or a folder a build wrote). Any URL is answered by the file in the folder
//! with the same last path segment, so the signed index keeps its real `https://` zip URLs and
//! the host checks in `scol_catalogue::fetch` still run on every URL. Never a redirect.

use std::path::{Path, PathBuf};

use scol_catalogue::fetch::{Fetcher, Step};

/// Host the virtual index URL uses: the host the index's zip URLs are limited to.
pub const MIRROR_HOST: &str = "github.com";
pub const INDEX_URL: &str = "https://github.com/local-mirror/index.json";
pub const SIG_URL: &str = "https://github.com/local-mirror/index.json.sig";

pub struct FileFetcher {
    dir: PathBuf,
}

impl FileFetcher {
    pub fn new(dir: &Path) -> FileFetcher {
        FileFetcher {
            dir: dir.to_path_buf(),
        }
    }

    fn resolve(&self, url: &str) -> Result<PathBuf, String> {
        let name = url.rsplit('/').next().unwrap_or("");
        let plain = !name.is_empty()
            && name != "."
            && name != ".."
            && !name.contains(['\\', ':', '?', '#'])
            && !name.chars().any(|c| c.is_control());
        if !plain {
            return Err(format!("cannot map {url} to a file name"));
        }
        Ok(self.dir.join(name))
    }
}

impl Fetcher for FileFetcher {
    fn fetch_bytes(&mut self, url: &str, max_bytes: u64) -> Result<Step<Vec<u8>>, String> {
        let p = self.resolve(url)?;
        let len = std::fs::metadata(&p)
            .map_err(|e| format!("{}: {e}", p.display()))?
            .len();
        if len > max_bytes {
            return Err(format!("{} is over {max_bytes} bytes", p.display()));
        }
        std::fs::read(&p)
            .map(Step::Done)
            .map_err(|e| format!("{}: {e}", p.display()))
    }

    fn fetch_to_file(
        &mut self,
        url: &str,
        dest: &Path,
        max_bytes: u64,
    ) -> Result<Step<u64>, String> {
        let p = self.resolve(url)?;
        let len = std::fs::metadata(&p)
            .map_err(|e| format!("{}: {e}", p.display()))?
            .len();
        if len > max_bytes {
            return Err(format!("{} is over {max_bytes} bytes", p.display()));
        }
        std::fs::copy(&p, dest)
            .map(Step::Done)
            .map_err(|e| format!("{}: {e}", p.display()))
    }
}
