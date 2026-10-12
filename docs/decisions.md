# SDK migration decisions

Owner: **scubamount**. Decisions recorded **2026-10-12**. These are product
policy and migration acceptance, not a claim that every stage is implemented.
No tag, publication or in-game certification follows from this record.

## Approved direction and current boundary

| Topic | Owner decision | Implementation boundary |
| --- | --- | --- |
| Feature preservation | Gold-standard target is **13 baseline feature units**: teleport, spawn, crew, loadout, npc, inspector, quantum, build, contracts, mining, multiplayer, creative and discord. **2 optional bridges** (titanlink, voxel_bridge) are counted separately and stay outside the normal release. No silent feature retirement or denominator reduction. | Step 1B externalizes inspector only. Ten built-ins remain, including NPC; creative and Discord were already external. This is not 13/13 completion. |
| Host and plugin dependencies | The Windows host uses the explicitly tagged sco-core submodule. Standalone plugins use verified published SDK archives, never a source-checkout include tree. | Baseline `sdk-v2.0.0`: SDK 2.0.0, plugin ABI 1.1, Star Citizen game pack 0.3.0. A future Star Citizen host-source kit is tracked in [sco-core #140](https://github.com/scubamount/sco-core/issues/140); a plugin SDK zip does not build the full host. |
| Enable/disable semantics | Eventually every feature has explicit per-folder enable/disable semantics, with no hidden built-in fallback. First-party feature folders are enabled by default and user-disableable. | Inspector ships enabled, without a `disabled` marker, and needs `plugins = on`. `plugins = off` makes inspector unavailable; it does not disable the ten remaining built-ins yet. This step changes neither creative's shipped-off default nor Discord's existing shipped-on default. |
| UI | Invest in SDK renderer-neutral widgets ([sco-core #139](https://github.com/scubamount/sco-core/issues/139)). Command-first examples meanwhile; no new ImGui coupling. | Inspector is command-only. Existing product UI is not removed by this decision, nor is widget support advertised as shipped. |
| Persistent build ownership | Retained build placements need scoped removable handles that survive plugin reload. No arbitrary entity deletion or private-pointer fallback. | Contract/provider work precedes build extraction; `keep()` alone does not grant persistent removal authority. |
| Crew ownership | Crew may remove only NPCs it spawned itself, not foreign seat occupants or another plugin's NPCs. | Enforce during crew extraction and legacy-path retirement. This record does not assert that all existing seat UI paths already implement the policy. |
| Console and cvars | Scoped game-cvar reads and reversible writes only. No arbitrary console execution exposed to plugins, MCP or peers. A local host debug console needs separate review and explicit opt-in. | No new console/cvar API is added by step 1B; legacy UI behavior is not silently certified. |
| Discord | Off until transport and transmitted fields receive privacy/security review. | This is the approved migration policy, **not an implemented default flip in step 1B**. Existing Discord packaging remains on; review and its separate consumer change are still required. |
| Multiplayer | Opt-in private co-presence; session control is host-only. Narrow user-consented LAN/VPN access; no stored peer secrets by default. | No official game servers, remote console, widened plugin session/socket authority or broad network exception. Network policy acceptance remains separate from synthetic transport tests. |
| Launcher, EAC and online safety | Keep launcher behavior unchanged in this step. Refuse mutation when EAC is present or offline status is unknown/online; no detection suppression or bypass. Owner security review of existing launcher/system policy is required **before step 6A** bootstrap extraction. | Plugins/MCP cannot change system/network policy. This record neither authorizes new EAC manipulation nor certifies current launcher behavior in game. |
| Runtime MCP | Minimal game-pack-owned snapshots plus optional diagnostic consumers; diagnostics explicitly opt-in. Track placement/implementation in [sco-core #141](https://github.com/scubamount/sco-core/issues/141). | Independently reviewed, local read-only observations with freshness/session validity and redaction. No gameplay invocation, mutation, raw memory/addresses, peer secrets or runtime reload. Static SDK discovery is not runtime connectivity. |
| Merge and gameplay evidence | PR merge is gated by CI; users test official releases. Owner-run Windows/in-game acceptance is **NOT RUN**. | Keep compile/check/synthetic evidence separate from gameplay, rendering and native Windows load evidence. No full migration completion or game-support claim until exact-build acceptance is recorded. |
| Updates and saves — open implementation policy | Step 1B changes no launcher updater or storage format; keep feature IDs and state stable during migration. | Atomic first-party updates, preservation/reset of explicit enable choices and legacy save/default contradictions remain to be resolved in their own work. No new updater behavior is approved or claimed here. |

## SDK work dependencies

The public SDK/game-pack gaps are tracked in **sco-core #122–135**. They cover
host composition/lifecycle, guarded player relocation, full-pose/spatial discovery,
owned prefabs, filtered rays/free camera, loadout, scoped cvars, quantum controls,
persistent/seat ownership, ASOP/hangar/ATC, contracts/missions, local economy,
host-only private sessions and content access reconciliation. These are dependencies,
not promises that the pinned SDK already exposes them.

- [#122](https://github.com/scubamount/sco-core/issues/122), [#123](https://github.com/scubamount/sco-core/issues/123), [#124](https://github.com/scubamount/sco-core/issues/124), [#125](https://github.com/scubamount/sco-core/issues/125), [#126](https://github.com/scubamount/sco-core/issues/126), [#127](https://github.com/scubamount/sco-core/issues/127), [#128](https://github.com/scubamount/sco-core/issues/128)
- [#129](https://github.com/scubamount/sco-core/issues/129), [#130](https://github.com/scubamount/sco-core/issues/130), [#131](https://github.com/scubamount/sco-core/issues/131), [#132](https://github.com/scubamount/sco-core/issues/132), [#133](https://github.com/scubamount/sco-core/issues/133), [#134](https://github.com/scubamount/sco-core/issues/134), [#135](https://github.com/scubamount/sco-core/issues/135)
- UI [#139](https://github.com/scubamount/sco-core/issues/139), host-source packaging [#140](https://github.com/scubamount/sco-core/issues/140), runtime snapshots/diagnostics [#141](https://github.com/scubamount/sco-core/issues/141)

Game-neutral kernel APIs stay neutral; engine signatures, offsets, hooks and guarded
mechanics belong to the game pack. Product plugins own feature policy, commands,
state and content through public contracts. Missing capabilities mean unavailable,
not a private engine fallback. Contracts land upstream before consumers move their
pin. See [architecture](architecture.md), [inspector tutorial](sdk-workflows.md)
and the [pending acceptance ledger](status.md).
