# NPC: external owned-lifecycle SDK reference plugin

Step 1C ships NPC as an **enabled external native plugin**, following inspector
in step 1B. Release layout is `data/plugins/npc/plugin.ini` plus `npc.dll`, with
no `disabled` marker. It requires `plugins = on`; the launcher's Plugins page can
disable its folder for the next game start. No host linkage, built-in registration
or hidden fallback remains. Nine built-ins plus four external features (npc,
inspector, creative, discord) preserve 13 baseline units; two optional bridges
are counted separately. Creative stays off and launcher behavior is unchanged. Step 1D separately
sets Discord folder metadata off and refuses presence pending privacy/transport
approval ([review](../discord/README.md)).

## Source and published contract

- `npc.cpp` / `npc.h`: public-service command and owned-lifecycle logic.
- `external.cpp`: `sco_plugin_query`, `sco_plugin_load`, `sco_plugin_unload`.
- `plugin.ini`: `id = npc`, independent version `1.1.0`, plugin ABI `1.1`,
  native entry `npc.dll`. Commands handle optional/missing game capabilities.

Build against published **`sdk-v2.0.0`**, combined **`sco-sdk-2.0.0.zip`**, after
verifying release **`SHA256SUMS`**. SDK 2.0.0, plugin ABI 1.1 and Star Citizen game
pack 0.3.0 are separate versions. Use only the extracted SDK's `include/`, not
private host headers, the dependency's source include tree or new ImGui inputs.
The Windows host independently builds from its tagged submodule.

The [worked tutorial](../../docs/sdk-workflows.md#worked-tutorial-standalone-native-npc)
separates **obtain/verify**, **isolated build/check** and **install/enable**. It
builds `sc-offline-npc` outside the checkout, builds the checker from the published
zip's `tools/sco-plugin-check.c`, invokes `npc.observe` with services absent and
runs only `npc-module-workflow` against synthetic public providers. Windows
x64/MSVC produces the game DLL; macOS/Linux modules are development-only.

## Commands and ownership

Invoke through the host command registry **on the game thread**:

| Command | Result |
| --- | --- |
| `npc.spawn <archetype> <count>` | Requests 1 to 10 owned NPCs near a fresh player pose; returned session IDs are immediately logged as pending streaming |
| `npc.observe` | Read-only pending/visible/removing/unknown counts and visible transforms in the log |
| `npc.remove <id>` | Requests removal of one ID still owned by this plugin |
| `npc.clear` | Requests removal of its remaining owned NPCs, never crew-wide or foreign deletion |

Choose an archetype for the actual game build; `synthetic_actor` in tests is not
a real-game recipe. Spawn/removal use `game.actors`, pose uses `teleport.spatial`
and observation uses `game.entities`. Borrowed service tables are reopened on
each command, not retained. Finite pose, a nonzero valid quaternion and a nonzero
streamed zone are required. Position/zone are used immediately; there is no ground
ray. Partial success reports how many IDs are already owned and returns the
failing result without losing those IDs.

`SCO_OK` from removal means **removal started**, not confirmed disappearance.
Removal may precede stream-in. Accepted removal relinquishes local ownership;
foreign IDs are rejected without mutation. Failed removal retains ownership for
retry; provider `NOT_FOUND` relinquishes the local claim. `alive=0` is ambiguous,
not proof of disappearance and not permission to recycle a removal ticket.
The local session-ticket bound is `SC_ACTORS_MAX_NPCS` per load, including
accepted-removal tickets until unload; it is distinct from the provider's global
live-NPC quota. Never persist session IDs, poses or zone IDs across loads.

Normal unload requests remaining cleanup once, then clears local state; repeated
unload is inert. The host withdraws owner-scoped commands and releases remaining
actor ownership. If cleanup is unavailable, the log explicitly requires host
owner-release cleanup. Process termination is not a successful unload.

## UI and native lifecycle boundary

No tab or hotkey is registered. The old NPC menu tab is removed pending
renderer-neutral widgets ([sco-core #139](https://github.com/scubamount/sco-core/issues/139));
Crew retains its shared archetype picker. Legacy `src/npc.cpp` stays untouched;
it is not a fallback for this plugin's commands. No new ImGui coupling is added.

Normal `sco.reload npc` is a running-host external-consumer route, not provider
hot-reload support or proven gameplay safety. Rebuild with the game closed or the
module unloaded; never overwrite a loaded Windows DLL. **Native faults are
restart-required, not safe reload/recovery after corruption.** Native plugins
execute trusted code, not sandboxed code; install only plugins you trust.

## Evidence and policy

**Windows/in-game acceptance: NOT RUN.** Checker acceptance and synthetic dynamic
loading do not prove game-provider behavior, rendering, owner-release cleanup or
real-host fault containment. Exact-build spawn/observe/remove/clear, missing-service,
disable, unload, ownership and fault checks remain in [status](../../docs/status.md).
Crew seat ownership and its separate regression are not certified here.

[Migration decisions](../../docs/decisions.md) retain policy boundaries, the
13-feature denominator and separately counted optional bridges. Launcher/EAC and
online refusal are unchanged; no bypass, remote console or runtime MCP gameplay
control is added. Licensing/provenance is unchanged: use the root
[LICENSE](../../LICENSE) and upstream SDK notices, not an assumption that the MIT
wire headers license the entire SDK or this plugin as MIT.
