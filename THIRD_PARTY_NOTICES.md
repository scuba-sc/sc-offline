# Third-party notices

sc-offline is licensed under the GNU General Public License version 3 (full text in
[LICENSE](LICENSE)) and is **based on ChrisWareOffline 0.9.0-rc1** by Chris Ware and
cloudyyrust (GPL-3.0). What this repository has changed since then, with dates:
[CHANGELOG.md](CHANGELOG.md).

The release (`sc-offline-<version>.zip`, and the `dinput8.dll` attached beside it on the
Releases page) also contains third-party code. Each component below keeps its own licence,
which applies to that code only; sc-offline's licence does not replace it. This file records
what the build compiles in and where it comes from. It is not a legal opinion.

## Where the code comes from

| Component | Where it comes from | Compiled into | Licence |
| --- | --- | --- | --- |
| sc-offline's own code | `src/` (except `src/third_party/`), `launcher/`, `plugins/` | `dinput8.dll`, `sc-offline.exe`, the plugin DLLs | GPL-3.0-only |
| [Dear ImGui](#dear-imgui-1928-wip) 1.92.8 WIP | `src/third_party/imgui/`, vendored in this repository | `dinput8.dll`, `sc-offline.exe` | MIT |
| [sco-core](https://github.com/scubamount/sco-core) | `external/sco-core` git submodule | `dinput8.dll` | GPL-3.0-only |
| [Lua](#lua-548) 5.4.8, modified | sco-core `plugins/lua/third_party/lua/` | `dinput8.dll` | MIT |
| [SQLite](#sqlite-3534) 3.53.4 | sco-core `third_party/sqlite/` | `dinput8.dll`, `sc-offline.exe` | Public domain |
| [toml++](#toml-340) 3.4.0 | sco-core `third_party/tomlplusplus/` | `dinput8.dll` | MIT |

The plugin DLLs (`data/plugins/*/`) are built from `plugins/` against sco-core's SDK
headers; NPC and inspector are built against the published SDK archive.

At this revision `external/sco-core` is pinned to `cd9a686bda4184f1befbdf0b74b503a1565540dd`
(tag `sdk-v2.0.0`). sco-core keeps its own
[`THIRD_PARTY_NOTICES.md`](https://github.com/scubamount/sco-core/blob/sdk-v2.0.0/THIRD_PARTY_NOTICES.md)
and [`docs/legal/third-party-inventory.md`](https://github.com/scubamount/sco-core/blob/sdk-v2.0.0/docs/legal/third-party-inventory.md),
with each component's source and file hashes. Lua, SQLite and toml++ are not in this
repository's own tree; they reach the release only through that submodule.

---

## Dear ImGui 1.92.8 WIP

`src/third_party/imgui/`: the ImGui core, the Win32 and Direct3D 11 backends, and the bundled
stb headers. Compiled into `dinput8.dll` (the in-game menu) and `sc-offline.exe` (the launcher
window). Upstream: <https://github.com/ocornut/imgui>. The licence also ships in the tree as
[`src/third_party/imgui/LICENSE.txt`](src/third_party/imgui/LICENSE.txt).

```
The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### stb libraries bundled with Dear ImGui

`imstb_rectpack.h`, `imstb_textedit.h` and `imstb_truetype.h` are copies of Sean Barrett's
stb_rect_pack.h 1.01, stb_textedit.h 1.14 and stb_truetype.h 1.26, which Dear ImGui modified
slightly (each file's header lists the changes). Each file offers two licences, MIT or public
domain (Unlicense); the MIT alternative reads:

```
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Lua 5.4.8

Via `external/sco-core` (`plugins/lua/third_party/lua/`), compiled into `dinput8.dll` so Lua
plugins can run. Source: <https://www.lua.org/ftp/lua-5.4.8.tar.gz>. **Modified** in sco-core:
the loops in `lstrlib.c` and `ltablib.c` charge a per-script step budget (`SCO_LUA_STEP`), and
`luaconf.h` includes `sco_lua_user.h`. sco-core marks every changed line `/* sco-lua */`.

```
Copyright © 1994–2025 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## SQLite 3.53.4

Via `external/sco-core` (`third_party/sqlite/`), compiled into `dinput8.dll` (sco-core's
`sco.storage` service, which keeps saves) and into `sc-offline.exe`. sco-core records it as
unmodified. SQLite is in the public domain; see <https://sqlite.org/copyright.html>. No notice
is required; it is listed here for completeness.

## toml++ 3.4.0

Via `external/sco-core` (`third_party/tomlplusplus/`), compiled into `dinput8.dll` as part of
sco-core's game pack, which uses it to read DataCore data packs such as
`data/builtin/quantum/datacore/quantum_drive.toml`. sco-core records it as unmodified.
Source: <https://github.com/marzer/tomlplusplus>.

```
MIT License

Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

---

## Not affiliated

This project is not affiliated with, endorsed by, or sponsored by Cloud Imperium Games or
Roberts Space Industries.
