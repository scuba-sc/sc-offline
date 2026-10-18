# Inspector: external read-only SDK reference plugin

Step 1B ships the native inspector as an **enabled external plugin**, not a
built-in. Release layout is `data/plugins/inspector/plugin.ini` plus
`inspector.dll`, with no `disabled` marker. It requires `plugins = on`; the
launcher's Plugins page can switch its folder off. There is no host fallback.
Step 1C also ships NPC as an enabled external command-first plugin. Nine built-ins
remain and four features are external (npc, inspector, creative, discord);
creative stays off. Step 1D separately sets Discord folder metadata off and
refuses presence pending privacy/transport approval ([review](../discord/README.md)).
See [NPC](../npc/README.md).

## Source and contract

- `inspector.cpp` / `inspector.h`: public-service reads and typed command logic.
- `external.cpp`: `sco_plugin_query`, `sco_plugin_load`, `sco_plugin_unload`.
- `plugin.ini`: `id = inspector`, independent version `1.0.0`, plugin ABI `1.1`,
  native entry `inspector.dll`. Game capabilities are optional so missing services
  can be reported by the command instead of preventing plugin load.
- `inspector.lua` / `lua-plugin.ini`: non-default restricted read-only alternative.
- `inspector_test.lua`: synthetic sandbox fixture, not bundled-runtime/game proof.

Build from published **`sdk-v2.3.0`**, combined **`sco-sdk-2.3.0.zip`**, after
checking the release **`SHA256SUMS`**. SDK 2.3.0, frozen plugin ABI 1.1 and Star
Citizen game pack 0.6.0 are distinct versions. Use the extracted SDK `include/`,
never private host headers, the source dependency include tree or new ImGui inputs.
The host itself uses the tagged sco-core submodule, not this plugin archive.

The [worked tutorial](../../docs/sdk-workflows.md#worked-tutorial-standalone-native-inspector)
contains the three stages: **obtain/verify**, **isolated build/check**, then
**install/enable**. It builds `sc-offline-inspector` from `plugins/` outside the
checkout, compiles the checker from the published zip's
`tools/sco-plugin-check.c`, invokes `inspector.read` with services absent, and
runs only the named `inspector-module-workflow` synthetic test. Windows x64/MSVC
produces the game DLL; macOS/Linux modules are development-only.

## Command and failure behavior

Invoke **`inspector.read`** through the host command registry on the game thread.
The legacy reply/log reports current player
actor/entity IDs, health and zone-local position/quaternion. Public `game.actors`
and `game.entities` tables are reopened for each read; no provider table or
session ID is retained across commands.

No player, missing provider/capability, truncated/null methods, failed calls,
non-finite data, zero zone or invalid rotation remain **unknown/unavailable**.
A health maximum of zero means unknown, not a zero-sized pool or a denominator.
The tool cannot certify engine state or guess fields the public service lacks.

### Candidate snapshot commands and optional panel

The expanded inspector requires the explicitly verified SDK 2.4.0 candidate
described in the worked tutorial. Published distribution defaults remain 2.3.0
until the human release step; the candidate is not a published release.

| Command | Required arguments | Behavior |
|---|---|---|
| `inspector.refresh` | none | Capture diagnostics and inventory once; advance generation |
| `inspector.status` | none | Read copied section status and historical generation |
| `inspector.stats` | integer page | Read one captured named actor stat |
| `inspector.gamerules` | integer page | Read one captured live field |
| `inspector.inventory` | integer page | Read one captured inventory port |
| `inspector.action_count` | none | Explicit metadata count acquisition |
| `inspector.action` | string name | Exact case-sensitive metadata lookup |
| `inspector.action_id` | integer id | Metadata lookup by nonnegative uint32 id |
| `inspector.params` | integer page | Acquire one definition, not a live value |

Pages are zero-based. Negative pages, wrong argument types and out-of-range
identifiers are rejected. Inventory pages report total, next-page and provider
truncated/limited flags. Replies that do not fit return `TOO_MANY`, never a
successful partial record. Unknown actions return `NOT_FOUND`; unavailable
captured sections return `UNAVAILABLE`.

When the host offers `sco.widgets`, panel `inspector.main` exposes Diagnostics,
Inventory, Actions and Parameters. Refresh captures diagnostics/inventory;
Lookup, Browse parameters and Refresh action count acquire metadata explicitly.
Drawing, tab selection and cached paging do not poll providers. Without widgets,
commands remain usable. A failed refresh replaces previous current values with
unavailable status; an unrefreshed snapshot remains historical, not live.
Build identity is provider-reported and not game-verified; synthetic hostd
fixtures are identified separately in test evidence.

The optional `SCO_SCUBA_HOSTD` CMake input registers `inspector_hostd_e2e` against
an already-built external hostd and the actual inspector module. It stages only
owned temporary plugin files, exercises synthetic and no-mock scenarios, then
checks process exit and port closure. This does not certify in-game rendering.

Unload clears borrowed API/owner state; the host withdraws owner-scoped commands.
The plugin allocates no subscription/worker context. `sco.reload inspector` is
an external command-consumer reload route in the running host; do not replace a
loaded Windows DLL. Rebuild with the game closed or module unloaded. This is not
provider hot-reload support, a native sandbox or recovery after corruption.

## Optional Lua companion example

With the game closed, copy `inspector.lua` into
`data/plugins/inspector_lua/` and rename `lua-plugin.ini` to `plugin.ini` in
that folder. The existing example retains its `inspector_lua` ID and
`inspector_lua.read` command. It is a language example, not another baseline
feature or a release default.
It uses only existing restricted `sco.game.actors` / `sco.game.entities` reads;
no mutation or spatial binding is added. It is not the release default.

## Evidence and policy

Checker acceptance and dynamic loading against synthetic public providers are
not actual game-provider, Windows rendering or gameplay evidence.
**Owner Windows/in-game acceptance: NOT RUN.** The required exact-build read,
unknown-value, disable and reload checks are in [status](../../docs/status.md).
Native plugins execute trusted code; install only plugins you trust.

[Owner decisions](../../docs/decisions.md) retain 13 baseline features plus two
separately counted optional bridges, require explicit folder semantics eventually,
and leave launcher/EAC/online refusal unchanged in this step. No bypass, remote
console or runtime MCP control is authorized. Project licensing/provenance is
unchanged; use the root [LICENSE](../../LICENSE) and upstream SDK notices, not an
assumption that the four MIT wire headers license the entire SDK/plugin as MIT.
