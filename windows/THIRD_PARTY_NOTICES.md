# SC Offline Desktop distribution notices

The desktop frontend and installer are GPL-3.0-only; the full licence is in
`LICENSE`. Their source is the `windows/` directory of
<https://github.com/kroryan/sc-offline>, under the `desktop-v0.1` release tag.

This distribution bundles the **unmodified published sc-offline 0.7.7 runtime**.
It is based on ChrisWareOffline 0.9.0-rc1 by Chris Ware and cloudyyrust.
The runtime's exact archive hash and source identity are in `runtime-lock.json`:

- sc-offline source: <https://github.com/scuba-sc/sc-offline/tree/1ac5308a65ac0ba7955e4108bd1dc800449cfa69>
- sco-core source: <https://github.com/scuba-sc/sco-core/tree/2197dfbf5a630e5a16a201526e628f1d28df908a>

For corresponding source with its pinned dependency, clone the public runtime
repository, check out the sc-offline source revision above, and initialize its
`external/sco-core` submodule. The desktop source and runtime source have separate
version numbers; this package does not rebuild or enable the current development host.

The native runtime includes third-party components. Their licences apply to those
components, independently of the desktop application's licence:

| Component | Licence text in this package |
| --- | --- |
| Dear ImGui | `licenses/imgui-MIT.txt` |
| stb libraries bundled with Dear ImGui | `licenses/stb-MIT.txt` |
| sco-core | GPL-3.0-only, full text in `LICENSE` |
| Lua 5.4.8, modified by sco-core | `licenses/lua-MIT.txt` |
| SQLite 3.53.4 | `licenses/sqlite-public-domain.txt` |
| toml++ 3.4.0 | `licenses/tomlplusplus-MIT.txt` |

`licenses/runtime-core-notices.md` is copied from the exact bundled sco-core
source revision; its relative source links refer to that upstream tree. The
Windows desktop code uses the Windows/.NET Framework platform libraries and does
not bundle their implementation. No Star Citizen game binaries, account files or
private logs are included.
