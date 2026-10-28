# Changelog

## Unreleased

- **Pin SDK 2.4.0 release candidate 2.** The host submodule, plugin archive
  pins, verifier digests, CI downloads and SDK-coordinate docs move from
  `sdk-v2.4.0-rc.1` (`48d7055`) to `sdk-v2.4.0-rc.2` (source `844e136`,
  combined archive SHA-256 `51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e`).
  RC1 evidence prose stays RC1. Candidate adoption only: final signed SDK
  approval and in-game acceptance remain separate; nothing was run in game.

- **Limit startup retry noise.** While offline admission is unavailable, the host
  retries at most every 500 ms instead of on every game message. Each attempt
  still performs a fresh admission check. Synthetic timing checks only; **not run in game**.

- **Development ASOP selection and host menu.** The spawn panel can edit a
  host-owned `asop_ships.txt` through the candidate SDK file service when the
  development composition is enabled; it is off by default. The separate host
  menu client is also off by default and uses widgets 1.3 numeric presentation. A saved selection is not a
  published or observed terminal list; native terminal and game acceptance remain
  unverified. The existing SDK pin is unchanged.

- **Inspector language setting.** Choose English or Russian for fixed Inspector panel labels and the Lua command description. The setting is independent of the launcher; structured diagnostic output remains English. Synthetic checks only; **not run in game**.

- **Develop gear presets against the SDK.** A standalone, read-only consumer captures and saves complete logical layouts and compares categories with manual advice. The current production host has no logical-layout producer, and the module is not staged; the workflow is verified only with synthetic host data and is **not run in game**.

- **Choose the launcher language.** Stable text keys, strict UTF-8 catalogues, checked named placeholders and an English/Russian/auto selector use the system language by default with built-in Russian and an English fallback. Existing control and command identities stay fixed. An optional `data/locales/ru.ini` can override Russian text; plugin language APIs stay separate. Checked headlessly and with synthetic files; **not run in game**.

- **Find one entry for each physical game installation.** Discovery combines paths only when both the channel directory and Bin64 match, preserving the first path and discovery priority. Relative and drive-relative paths use the actual channel parent for user files and checks. Unavailable directory identity keeps the existing path comparison. Separate installs and channels remain separate even when they share executable files or a Bin64 junction. Checked with synthetic filesystem fixtures; **not run in game**.

- **Loadout friendly names and shared search.** Curated selectors show seven attributed public tool/canister labels alongside their unchanged class IDs. The shared filter matches names/classes without clearing or rebinding selected choices; apply/jobs keep their existing ordering, admission and partial/cancel behavior. No defaults, native compatibility or release staging change. **Not run in game.**

- **Quantum controls show received GameRules.** The existing panel shows allowed/forbidden/unknown quantum travel and boost gates, pauses new UI boosts when rules are unreadable or forbid them, and rechecks before starting. Owned release remains available; group membership is never inferred. Automatic presentation reads occur at most once per 250 ms. Relocation and provider-authorized command behavior, native/default settings and the SDK pin stay unchanged. **Not run in game.**

- **External Build and SQ42 picker.** The existing Build owner now publishes grouped categories, global search, 64-row pages and SQ42 placement controls through public widgets and one explicit `lists/buildables.txt` data-pack snapshot. Row changes clear selection; full identifiers reach the existing guarded commands. Missing/ambiguous/malformed content and failed publication refuse safely. No raw-file import, pack staging, backend/default or SDK pin change. **Not run in game.**
- **Read copied contract references.** External Contracts adds bounded summary and single-reference commands for explicit `lists/contract_scripts.txt` data packs through published SDK 2.4.0 RC1. Source GUIDs remain metadata; native XML installation is explicitly unknown. No mission/economy calls, implicit imports, retained engine, staging, default or pin changes. **Not run in game.**
- **Development Loadout curated selectors.** Add fourteen independent public weapon, attachment and tool selectors from an explicit `lists/loadout_slots.txt` data pack. Unchanged omits a slot; apply and jobs retain existing version, capability, ordering and partial-result rules. The optional 344-choice catalogue uses attributed public project identifiers and is never imported or staged automatically. No native/default or SDK pin changes. **Not run in game.**

- **Curated Travel locations use public data packs.** The external Quantum consumer reads one explicit `lists/locations.txt` startup snapshot through published SDK 2.4.0 RC1. Missing, ambiguous or malformed curated lists refuse without a raw-file fallback. The original base file is untouched; users must explicitly copy their curated list into an ordinary pack while stopped. Generated locations/bookmarks keep their existing persistence policy. No pack staging, backend/default or SDK pin changes. **Not run in game.**

- **Development Loadout manual slots.** Add explicit public SDK 2.4.0 RC1 weapon/attachment/multitool assignments to the optional Player panel and commands, plus guarded async submit/progress/cancel. Copied legacy catalogue ports remain unsupported; no None/clear or tool-default preservation is inferred. Synthetic public-provider tests cover refusal, partial results and job lifetime. Loadout remains outside production staging; no backend or capability default changes. **Not run in game.**

- **Keep your current Crew seat.** If you queue a move to another seat and then
  select the seat you are already in, Crew now cancels its older queued move.
  This stops future consumer retries; it does not undo a native link already
  accepted by the game. Checked with synthetic services; **not run in game**.
- **Development Loadout panel.** Empty gear, outfit or buildable lists no longer
  cause a widget-panel replacement to be rejected, including after SQ42 spoiler
  confirmation. Loadout remains outside release staging; this does not add slot
  mappings or verify native equip behavior. Checked with synthetic services;
  **not run in game**.

- **Clarify legacy startup settings.** The launcher still reads and passes
  `start` and `start_ship`, but the current external spawn module refuses
  destination startup. INI comments and launcher docs no longer promise the old
  timed Daymar arrival or boarding. Wallet precedence is unchanged; this does
  not add a startup feature or confirm the original settings report in game.

- **Resize the menu.** Drag its native window edges or corners; the size is saved
  in `data/menu-window.ini` when the drag finishes and restored on the next run.
  The window is bounded by the monitor work area, including smaller displays.
  Long tab titles keep their width and scroll instead of shrinking. Headless
  geometry, file/restart and actual ImGui overflow checks cover the change;
  Windows rendering and game acceptance remain pending. **Not run in game.**

- **Linux evidence is dated.** Record the reported sc-offline 0.7.0/Bazzite Wine run with its exact game/toolchain tuple, while retaining experimental status and the current SDK 2.4.0 RC1 acceptance gap. No launcher behavior changed or new game run was performed.

- **Inspector snapshots on the published SDK 2.4.0 RC1.** Integrate the public consumer commands and retained widget panel from PR #147 without restoring its obsolete SDK 2.3.0 defaults or unpublished archive pin. Manual refresh and cached paging expose explicit unavailable states; missing providers remain unavailable. Refresh returns to the first page of the new snapshot, including when the inventory shrinks. Configurable proof generators retain the hosted VS 2026 default. Local synthetic checks are separate from Windows, external-host and game acceptance. Tracking: #155.

- **Host and plugins use the same SDK candidate.** Update the embedded sco-core host from `e616ed6` to exact `sdk-v2.4.0-rc.1` source `48d7055ef49e1b073ec59e4ce8ded7a6006329eb` (SDK 2.4.0, game pack 0.8.0, unchanged plugin ABI 1.1). External plugins retain their existing checksum-pinned RC1 archive. The host now includes the candidate service contracts and signature rows for build 4.10.197.2878; default-off and unavailable backends remain deferred. This does not approve a final SDK release. **Not run in game.**

- **One SDK for every external plugin: SDK 2.4.0.** The creative, discord, crew, spawn, build, mining, teleport and quantum plugins and the inspector, npc and contracts proof jobs now build and check against the published sco-core SDK 2.4.0 release candidate 1 ([`sdk-v2.4.0-rc.1`](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.1), combined `sco-sdk-2.4.0.zip`, game pack 0.8.0, verified game build 4.10.197.2878). The older SDK release downloads are gone; the final signed 2.4.0 release will follow. Plugin ABI 1.1 is unchanged and no plugin behaviour changes: this is a build-input bump only (spawn's `spawn.startup` refusal text no longer names an old SDK). The Windows host's `external/sco-core` pin is unchanged. Checked on a development machine with synthetic services and the SDK's plugin checker. **Not run in game.**

## 0.7.8.3 (2026-10-15)

- **The Vehicles panel opens again.** In 0.7.8.x the spawn plugin's panel was refused at load (`[spawn] warning: Vehicles panel refused` in `mod.log`) because its empty owned-ship list broke the panel rules; spawn commands kept working. Not yet checked in game.
- **The Creative tab is back.** 0.7.8 dropped the creative checkboxes from the menu; the creative plugin now adds its own **Creative** tab with God mode, Noclip, Infinite ammo and Infinite ship ammo toggles, a Noclip speed slider (1 to 10000 m/s, starting from the `noclip_speed` setting) and a line showing the last result or why the game refused it. A refused toggle flips back to the real state. Creative stays off by default: turn it on from the launcher's Plugins page. Without a menu renderer the plugin logs a warning and its `creative.*` commands keep working.
- **No stray status line.** The menu's status strip no longer shows "Pick a ship and press Spawn." under every tab; it stays empty until something sets a status.
- **Not checked in game.** The Creative tab and the empty status strip were built and checked on a development machine (synthetic services and the SDK's plugin checker); neither has been looked at in the game.
- **The build plugin ships.** `data/plugins/build` is now in the release download, enabled, built against the published sco-core SDK 2.2.0 like the spawn and crew plugins. What it does: `build.place <class or ObjectContainers/.../name.socpak> <metres ahead, 0-100>` places an object on the ground ahead of you, `build.undo` removes the last one you placed, and `build.clear` removes everything it placed. Removal is requested, not confirmed: an object may take a moment to go. `build.status` reports the counts.
- **What does not work yet.** Interactive build mode is not available: F6 (`build.toggle`), the free camera, aiming, the preview, and `build.select`, `build.aim`, `build.drop`, `build.rotate`, `build.reach` and `build.camera` all answer that the game service they need is not available. Placed objects are not kept across a plugin reload or a game restart; unloading the plugin removes them.
- `build.undo` with nothing to undo now answers OK ("Nothing retained to undo") instead of an error, as the SDK's plugin checker requires of commands that take no arguments.
- **Not run in game.** The plugin was built and checked on a development machine against the published SDK with no game services present; placing and removing objects in the game has not been tried.
- **Teleport and Travel move you through sco-core's `game.relocation`.** F7 (`teleport.save`) saves where you are again and F8 (`teleport.go`) takes you back; `quantum.travel <destination> <altitude>`, `quantum.bookmark <name>` and the Travel tab's buttons take you to a destination or a saved bookmark in the star system you are in. Each move is one request to sco-core's public `game.relocation` 1.0 service, which only moves you in an offline session it has confirmed, with a live player and a known star system. The plugin follows the request every game tick and tells you when you arrive, or why the move failed or was cancelled. One move at a time.
- **Moves need this host.** The host now pins sco-core `main` at `e616ed6` (between SDK releases, after 2.3.0), which provides `game.relocation`'s production path and `game.quantum`'s backend. Moves and the Caps Lock boost work only while offline admission is positive, the engine rows resolve, there is a live player and a known star system; otherwise every move answers that it is unavailable and nothing moves. Saving spots and bookmarks, inspecting them and scanning work as before, with the same files and databases.
- **Two switches.** Each plugin has a `relocation` setting, "Allow moving the player (offline only)", on by default; off makes every move command answer unavailable. `relocation = on|off` in `sc-offline.ini` (default on) switches moving off for every plugin. The host switch is compiled in with this pin, through sco-core's `SetRelocationEnabled`. Neither switch skips the offline check.
- **teleport and quantum ship.** `data/plugins/teleport` and `data/plugins/quantum` are in the release download, enabled, built against the published SDK 2.3.0 (`tools/check_teleport_sdk.py`, `tools/check_quantum_sdk.py`). The teleport plugin is now called "Quick-save and return". The quantum boost controls are unchanged.
- **Quantum boost has an engine backend.** The Caps Lock boost now runs on sco-core's `game.quantum` under its amended rule: it works only offline, and the original values are restored on release. It is untested in game.
- **Host pin.** `external/sco-core` moves from `sdk-v2.2.0` (`686187c`) to sco-core `main` at `e616ed6`, after the published `sdk-v2.3.0`. The external plugins still build against the published SDK archives.
- **Not run in game.** Checked on a development machine with synthetic services and the SDK's plugin checker (no game services). No move has been tried in the game, and where `quantum.travel` puts you (the destination radius plus the altitude, along the destination zone's own up axis) has not been checked.

## 0.7.8.2 (2026-10-15)

- **Contracts work again.** Contracts run on the mod's own contracts engine, as in v0.7.7: the mobiGlas lists them about 15 seconds after you spawn, accepted missions run, rewards are paid, and kill objectives count deaths again (intended behaviour carried over from v0.7.7; listing, accept, completion and rewards on this host engine are **not verified in game**, see `docs/status.md`). Your balance is kept in `data/wallet.txt`. The `contracts.status` command from v0.7.7 is not back, and `data/storage/contracts.db` is no longer updated. The `contracts` plugin still ships; it answers that its game services aren't available yet and does nothing else.
- Startup was confirmed on a second Windows PC with the game running with normal rights: the offline check passed, every offline patch applied, and the game loaded into the universe.
- The optional co-presence module's source now lives in its own repository, [scubamount/sc-mp](https://github.com/scubamount/sc-mp). sc-offline no longer builds it; releases since 0.7.8 have not included it, so nothing you download changes.

## 0.7.8.1 (2026-10-15)

- **The game starts again ([#134](https://github.com/scubamount/sc-offline/issues/134)).** v0.7.8 refused its own offline startup on every PC: `mod.log` showed `[app] offline admission/prepare refused: BAD_ARG`, no offline patch ran, the game closed about a quarter second after it opened, and the launcher then took the mod out again. Two causes, both fixed. The host handed sco-core's offline startup a built-in plugin it no longer accepts, and the mod could not show that the game was offline, so startup was refused. Behind that, startup had moved off the game's loading path, so the game ran ahead of the mod and closed before the offline patches landed.
- **How the mod now knows the game is offline.** Inside the game, it reads Windows Firewall's rules for this exact `StarCitizen.exe` (the rules the launcher adds: everything outside your local network blocked, in and out, on every active network profile) and checks that the game has no open connection to the internet. Anything it can't read counts as not offline, and the mod refuses to start. `mod.log` shows one `[g1] evidence:` line with the result.
- **The game waits for the mod at start-up.** The game is held at its first instruction until the mod's startup has finished (up to 60 seconds), so expect a pause of roughly 15 seconds before the game window appears.
- **Contracts is a plugin now, and unavailable for the time being.** It leaves `dinput8.dll` and ships as `data/plugins/contracts`. It needs the missions and economy services that the pinned sco-core SDK 2.2.0 does not provide yet, so contracts commands refuse in game until a later SDK publishes them. Your wallet balance in `wallet.txt` is kept.
- README: corrected recovery steps (status light, `uninstall`), current SDK status, and a new "What keeps you offline and safe" section.
- Version 0.7.8.1; the release tooling accepts a fourth version number.

## 0.7.8 (2026-10-14)

- **Pin the published sco-core SDK 2.2.0 release.** `external/sco-core` moves from `sdk-v2.1.0` to signed tag `sdk-v2.2.0`, approval commit `686187ce936400450d448f1fe754b1e58263e4af` (its archives were built from source commit `81437e1a12993a4484308bd76443a003ae57b7fa`). SDK **2.2.0**, plugin ABI **1.1** (unchanged), Star Citizen game pack **0.5.0**. The standalone-plugin CI jobs download `sco-sdk-2.2.0.zip` and verify it against the release `SHA256SUMS`. SDK 2.2.0 is additive (`sco.widgets` 1.1, `game.hangar` and `game.reputation` 1.0, `game.world` 1.1). The maintainer smoke-tested the R1 build in game on 2026-10-14; individual features were not separately verified in game.
- **Pin the published sco-core SDK 2.1.0 release.** `external/sco-core` moves from tag `sdk-v2.0.0` (`cd9a686bda4184f1befbdf0b74b503a1565540dd`) to signed tag `sdk-v2.1.0`, commit `8ef24243b4b8ee45fb49bde4192bdbdfa7473ace` (the release approval commit; its archives were built from source commit `77c328fc088f85336f1241c0c054bc461fe8b64c`). SDK **2.1.0**, plugin ABI **1.1** (unchanged), Star Citizen game pack **0.4.0**. The standalone-plugin CI jobs download `sco-sdk-2.1.0.zip` and verify it against the release `SHA256SUMS`. SDK 2.1.0 publishes the widgets, content, mining, prefab, quantum, game-spatial and private-session headers that entries below describe as missing from the published pin or available only from a local integration SDK, and adds the Star Citizen host-source kit `sco-sdk-host-source-sc-2.1.0.zip`; those entries describe the pin at the time of each change. Windows/MSVC builds and in-game acceptance on this pin are pending: **not run in game**.
- **G13 multiplayer is an enabled external module, not a host built-in.** Preserve `multiplayer.status`, `multiplayer.leave`, `multiplayer.goto`, the Multiplayer Host/Join panel and the existing `storage/multiplayer.db` keys. Explicit UI actions use the default-on, admission-gated host broker; default loopback/no startup socket, private LAN only with host-owned exact bind/peer policy. Passphrase display is masked, remembering defaults false, and disabling remembering deletes the optional plaintext key transactionally. Ghosts use public copied poses and owned actor/entity services; malformed/foreign/stale peer messages refuse. G2 goto remains an explicit refusal. The neutral host now renders retained widget panels. Standalone build/install requires a newer G13 SDK; published `sdk-v2.0.0` and CI pin remain unchanged, so release-job promotion is pending that SDK. Windows/MSVC, renderer and real offline-session/LAN acceptance are **NOT VERIFIED**.

- **Steps 4A/4B: external loadout development module on G6/G7.** Add `plugins/loadout`, copied public content-list parsing, typed bounded multi-slot requests with input-ordered partial/refusal replies, session-only spoiler confirmation, renderer-neutral Player/SQ42 panels, and public spawn/build command shortcuts. Four SQ42 settings use real `game.cvars` reads and display explicit unknowns; controls remain read-only and write/console commands refuse even if a write provider exists. Remove the legacy host registration, source/link inputs and tick fallback; preserve inactive sources under `legacy/loadout`. Unsupported weapon/clothing/head/material slots and old implicit XML/default/clear behavior remain explicit pending parity, not private fallbacks. SDK 2.0.0 pin is unchanged: configure this module against a newer local SDK; release promotion awaits approved SDK/host providers. **4C quantum control is NOT approved:** the existing private boost/keeper stays untouched. Compile-only development evidence; tests, Windows/MSVC, renderer and gameplay **NOT RUN**.

- **Approved G8 quantum consumer:** the external `quantum` module now composes bounded `game.quantum` controls with the existing Travel/bookmarks panel and unchanged owner-scoped `quantum.db` format. Add explicit `quantum.enable`, `quantum.hold` (1..1000 ms), `quantum.release`, `quantum.boost` and `quantum.control_status`. No startup enable; focused Windows Caps Lock can refresh a 150 ms input lease after explicit enable, primary mouse input is opt-in and does not infer NAV mode. G8 owns drive/charge/audio/effects; remove the private boost source from host build/startup and disable the global quantum cvar keeper (G7 writes remain unapproved). Existing travel command IDs and G2 relocation refusal remain unchanged. Core's approved provider activates only with a grounded backend plus installed owner cleanup; the default production engine adapter is still absent, so actual boost/effects and NAV interception remain unavailable rather than falling back to private offsets. One actual-module synthetic G8 workflow passed on macOS; Windows/MSVC, focused input, renderer, DB import and game/effect restoration are **NOT VERIFIED**. Published SDK/CI pins unchanged.

- **Steps 4A/4B: external loadout development module on G6/G7.** Add `plugins/loadout`, copied public content-list parsing, typed bounded multi-slot requests with input-ordered partial/refusal replies, session-only spoiler confirmation, renderer-neutral Player/SQ42 panels, and public spawn/build command shortcuts. Four SQ42 settings use real `game.cvars` reads and display explicit unknowns; controls remain read-only and write/console commands refuse even if a write provider exists. Remove the legacy host registration, source/link inputs and tick fallback; preserve inactive sources under `legacy/loadout`. Unsupported weapon/clothing/head/material slots and old implicit XML/default/clear behavior remain explicit pending parity, not private fallbacks. SDK 2.0.0 pin is unchanged: configure this module against a newer local SDK; release promotion awaits approved SDK/host providers. At this earlier step 4C was pending; the approved G8 consumer entry above supersedes that decision and retires private boost/keeper activation. Compile-only development evidence; tests, Windows/MSVC, renderer and gameplay **NOT RUN**.

- Integrate external creative/Discord/crew/spawn folders from published SDK builds; remove crew/spawn built-in/UI/tick fallbacks and route ordinary SQ42 shortcuts through spawn commands. Creative/Discord stay off; widgets, destination startup and enemy-wing orchestration remain explicitly deferred. Windows/game checks not run.

- **Step 1D: strengthen the external creative plugin (1.1.0).** Keep the shipped `disabled` marker and all six command IDs. Reopen public creative/settings services with full member-extent/null/capability checks; refuse invalid arguments and retain provider failure results. Declare owner-scoped `noclip_speed` (float 10, range 1–10000), read only on an explicit noclip-on request; no startup auto-enable or settings-write path. Add a standalone published-SDK build/install target and `tools/check_creative_sdk.py`, reusing inspector archive/checksum/dependency machinery and the published checker. The focused actual-module fixture covers physical truncated tails, failed reopen, settings, stale callbacks and clean reload under ASan/UBSan; toggle/speed release uses a labeled synthetic provider-hook stand-in. [Creative build/check/install and lifecycle documentation](plugins/creative/README.md) distinguishes consumer evidence from actual host/game cleanup. Windows/MSVC and owner in-game acceptance remain **NOT VERIFIED**; no SDK ABI, Discord policy, private allowlist or launcher changes.
- **Step 2A: command-first external Crew module on published SDK 2.0.0.** Add standalone `plugins/crew` and the focused `check_crew_sdk.py` archive/build/install proof, following the inspector/NPC public-consumer boundary. Preserve `crew.target`, `crew.sit`, `crew.stand_all`, `crew.fill`, `crew.clear` and `crew.power_on`; only public vehicle/actor/spatial services are used, with fresh complete-extent/null-checked tables and no private UI/spawner fallback. Crew owns bounded seat jobs and removes only NPCs it spawned; local-player sit/eject uses the explicit published vehicle permission, never foreign occupant removal. Partial seats/spawn, unusable/unknown seats, timeout/refused removal, retained session tickets, first-tick clock anchoring and unload twice are covered by an actual loaded synthetic module. **UI regression:** the external module has no former Crew tab/seat or shared NPC picker; replacement waits for `sco.widgets` ([sco-core #139](https://github.com/scubamount/sco-core/issues/139)), absent from the pinned release. Enabled standalone install requires `plugins = on` and host registration/link/fallback removal in the parent integration; this consumer commit does not itself change host composition. **Windows/game acceptance: NOT RUN.** Published checker, dependency controls and synthetic owner-release stand-ins are not gameplay/fault-cleanup certification.

The entries below record changes in order, newest first. Earlier migration counts
and routes describe intermediate states; Step 1C supersedes the built-in NPC path.

- **Step 1D: fail closed on Discord presence pending privacy/transport approval.** Discord's folder metadata now ships an empty `disabled` marker and inactive bool preferences default false, as required by [owner decisions](docs/decisions.md). Manually enabling its folder loads only `discord.status`, which returns `SCO_UNAVAILABLE` with the explicit blocker; kept true preferences never authorize transmission. Remove the direct Discord named pipe, activity payload, game reads and unmanaged retry worker instead of adding a custom IPC/network exception. Published SDK 2.0.0 has no approved Discord transport. The feature remains in the 13-unit inventory, blocked rather than retired. A standalone published-archive proof builds the actual module, tests settings/refusal/lifecycle, checks exact compiler dependencies, rejects private-header/symbol mutants and runs the archive's checker with absent services. [Discord review and tutorial](plugins/discord/README.md) record the old transmitted fields and distinguish local delivery from Discord publication. Shared host/CI release wiring is an integration task; this entry does not assert it is complete. **Windows, Discord transport/privacy, real host reload and in-game acceptance: NOT RUN.** Creative stays off; no launcher, firewall, EAC, ABI or allowlist change.
- **Step 2B consumer: ordinary spawn is a standalone command-first SDK plugin.** `plugins/spawn/` builds independently from verified published `sdk-v2.0.0`, with an enabled `spawn` manifest and independent version 1.0.0. Preserve `spawn.ship <class:string> <height:float>`; add typed boarding/power options, choose-later boarding, status and owned cancel/remove/clear. Use `game.entities` ownership for removal, never cross-delete `spawn_as` IDs or relinquish with `keep`; Flight Ready requires confirmed pilot occupancy and a fresh public aboard-ship reading. Streaming/seat retries are bounded, removal tickets are retained, and foreign/crew occupants are never evicted. Destination startup explicitly refuses G2/G3. `tools/check_spawn_sdk.py` reuses public archive/header proofs, dynamically loads the real module, checks missing providers, and runs dependency/symbol/partial-tail negative controls. UI awaits renderer-neutral widgets. Host registration/link/tick-fallback removal and release composition are separate integration work; this consumer commit leaves legacy `spawner.cpp` and `npc.cpp` untouched. **Windows/in-game acceptance: NOT RUN.**
- **Step 1C: ship NPC as an external published-SDK plugin.** `data/plugins/npc/{plugin.ini,npc.dll}` ships enabled without a `disabled` marker and requires `plugins = on`. `npc.spawn`, `npc.observe`, `npc.remove` and `npc.clear` are command-first, with bounded session tickets, fresh borrowed services, explicit pending/unknown observations and owned-only removal; accepted removal is not confirmed disappearance. Remove NPC built-in registration and host linkage, with no disabled-folder or `plugins = off` fallback. Nine built-ins remain, alongside four external features (npc, inspector, creative, discord): 13 baseline units plus two separately counted optional bridges. The old NPC tab is removed pending renderer-neutral widgets ([sco-core #139](https://github.com/scubamount/sco-core/issues/139)); Crew keeps its shared picker, no new ImGui coupling is added, and legacy `src/npc.cpp` is untouched. Isolated build/check/install uses verified published `sdk-v2.0.0` combined `sco-sdk-2.0.0.zip` and release `SHA256SUMS`. Launcher behavior and creative/Discord defaults are unchanged. **In-game acceptance: NOT RUN.** Native faults are restart-required, not safe reload/recovery; checker and synthetic evidence do not certify game behavior.
- **Step 1B: ship inspector as an external SDK plugin.** At this earlier step, only inspector's production built-in registration was removed; ten built-ins, including NPC, remained (NPC is externalized by step 1C above). `data/plugins/inspector/plugin.ini` and `inspector.dll` ship enabled without a `disabled` marker, require `plugins = on`, and expose read-only `inspector.read` with explicit unknowns and no new ImGui coupling. Native plugin compilation is isolated against the published `sdk-v2.0.0` combined `sco-sdk-2.0.0.zip`, verified against release `SHA256SUMS`; the restricted Lua example remains non-default (its own `inspector_lua` ID). A worked published-SDK build/check/install tutorial and [owner decisions](docs/decisions.md) record the 13-feature target, separately counted optional bridges and pending policy work. Creative/Discord defaults and launcher behavior are unchanged. Windows and owner in-game acceptance remain **NOT RUN** until separately recorded; checker/synthetic success is not gameplay proof.
- **Pin the published sco-core SDK 2.0.0 release.** `external/sco-core` moves from `2197dfbf5a630e5a16a201526e628f1d28df908a` to tag `sdk-v2.0.0`, commit `cd9a686bda4184f1befbdf0b74b503a1565540dd`. The plugin ABI remains **1.1** and the Star Citizen game pack is **0.3.0**; the SDK major version describes packaging/profile and release-process changes, not an ABI bump. The full host continues to build from tagged source. Architecture and status now identify the release pin; older entries below describe their pins at the time of each change. Offline startup, EAC detection/refusal, service-before-plugin ordering, menu and clean exit remain owner-run acceptance: **not run in game**.
- **Optional plugin ImGui is exact-version coupled to the host, not the frozen C ABI:** independently pin all 15 vendored inputs and compare the three public SDK files; CI rejects source/configuration drift even when the WIP version label and both header copies agree. Drawing plugins must rebuild when the host pin or ABI-affecting configuration changes. No runtime, SDK pin or gameplay behavior change.
- **SDK-only inspector and owned NPC workflows.** Shared feature sources now serve separate built-in and native adapters without private menu, registration or version dependencies. `inspector.read` (and a restricted Lua counterpart) reports player/health/transform reads with explicit unknowns; `npc.observe`/`npc.remove` add bounded lifecycle observation and owned-only removal. The NPC tab no longer clears crew NPCs. Synthetic SDK-only and adapter tests are included; Windows builds and in-game acceptance remain required. See [SDK workflows](docs/sdk-workflows.md).
- **Built-in commands answer `SCO_FAILED` when they ran and failed** (sco-core #12; `SCO_FAILED = 7`, "the command or service ran, but failed; see reply"). `teleport.save` and `teleport.go` answer `SCO_FAILED` with the reason in the reply when the spot couldn't be saved or reached (no saved spot yet, the spawn file it couldn't write) instead of `SCO_UNAVAILABLE`, and `multiplayer.leave` does the same with no session to leave. `SCO_UNAVAILABLE` stays what it says: the capability isn't on this build, which the host answers before the command runs — so a plugin or a menu can tell "teleport is missing here" from "teleport ran and this call failed". The Player tab's creative checkboxes say "Not available on this game build." for `SCO_UNAVAILABLE` and show the command's own reply for `SCO_FAILED`. The decision is one function (`src/builtins/builtin_result.h`), tested on Linux by the check job (`tools/builtins_result_test.cpp`). Plugins see the change only through the result code they already handle; 1.0 plugins that treat both as "didn't happen" are unaffected. The published `sdk-v2.0.0` pin (`cd9a686`) retains `SCO_FAILED` 7, its ABI pin, the docs table and the sco-lua mapping.
- **sco-core moves to `c133484`** (from `31cf53a`; sco-core's release-hardening PRs 01–05). sco-lua is game-neutral now: Lua scripts' `sco.datacore`, the read-only `sco.game.*` tables and the `game.*` event payloads come from sco-core's Star Citizen adapter, which `StartHostKit` registers before any script loads (`sco_lua_add_extension(&sco_lua_game_extension)`; `dinput8` links `sco_lua_game`). Same tables, same read-only rules, same payloads as before; a failed registration is logged. Also in the move: the SDK manifest `sdk/components.json` (the `sdk-headers` job reads it, core + game-sc), the profile zips, IP policy and notices. Needs the usual in-game check: a Lua script reading `sco.game.actors` and receiving `game.player.died`.
- **Docs rewritten for the SDK conversion.** The README and `docs/` now describe the code as it is: sc-offline as host and reference consumer of sco-core (pinned at `31cf53a`), the `game.*` service each feature runs on (new `docs/architecture.md`), built-ins versus optional plugins and the creative plugin, the five CI jobs and what the `sdk-headers` allow-list means (`docs/build.md`), and what has not been run in game yet (new `docs/status.md`). Fixed: the plugin report counts (ten built-ins; `creative` is found but not loaded while its `disabled` marker is there), the capabilities each built-in's commands are gated on, the Multiplayer tab missing from the tab order, co-presence listed as planned and not shipped, ASOP terminals listed as not working offline, and the old one-feature-at-a-time roadmap, now marked historical. No code changes.
- **Noclip, god mode and infinite ammo are an optional plugin now** (`data/plugins/creative`, CMake target `sc-offline-creative`, sources in `plugins/creative/`; pins sco-core's `game.creative` 1.0). The plugin is a plain SDK consumer: it includes only the SDK zip's headers, and `tools/sdk-headers.py` now checks `plugins/` as well as `src/builtins/`, with no allow-list entry for it. It registers `creative.god <on>`, `creative.noclip <on>`, `creative.noclip_speed <speed>`, `creative.ammo <on>`, `creative.ship_ammo <on>` and `creative.status`, and the release ships it **off** (a `disabled` marker next to `plugin.ini` and `creative.dll`; turn it on in the launcher's Plugins page, with `plugins = on`). **Removed from `dinput8.dll`**: the `ammo` built-in (`ammo.infinite` and `ammo.ship_infinite` are `creative.ammo` and `creative.ship_ammo` now, because a plugin's commands start with its id), `src/ammo.cpp`, the spawner's noclip and god-mode code and their `Menu_Set*` toggles, and `PlayerShipId`, `TargetShipId` and `ShipPartComponents`. The Player and Vehicles tabs' checkboxes run the plugin's commands through `invoke` (with the plugin off they put the checkbox back and say so in the status strip). Behaviour changes: god mode no longer starts on, and infinite ship ammo covers the ship you're aboard only, not the Crew tab's target ship. The build job uploads the plugin's DLL (artifact `creative-plugin`) and the release job packages it. Not run in game yet.
- `src/builtins/` is gated to the sco SDK's headers: a new `sdk-headers` CI job (`tools/sdk-headers.py`) fails any include there that isn't an SDK zip header or a file in `src/builtins/`, with file and line. What the SDK can't express yet is listed, one reason per header, in `tools/sdk-headers-allow.txt` (24 entries covering 64 includes; the list fails when a line goes stale, so it only shrinks). The built-ins get the product version through `builtins.h`, and `npc_plugin.cpp` takes the registration type from it too. The `no-scans` job already runs on pushes to `main` as well as on pull requests. The creative features (noclip, god mode, infinite ammo, infinite ship ammo) moved out of `dinput8.dll` into the optional `creative` plugin (see the entry above).
- Build mode reads the game through sco-core's `game.world` 1.0 (`sc_world.h`) where it can: the ground ray that skips nothing (`build.place`, the NPC tab's placement, the contracts and voxel bridge ground checks) is `game.world.raycast`, and the camera build mode aims from is `game.world.camera`. The aim ray that follows your cursor stays on sco-core's build rows, because it must skip the preview prop and `raycast` 1.0 skips nothing. When `game.world` or its `game.world.raycast` / `game.world.camera` capability isn't ready, `mod.log` says so once (`[build] game.world isn't available ...`, `... raycast isn't ready ...`, `... camera isn't ready ...`) and build mode uses the rows as before. sc-offline has no teleport-to-camera feature, so nothing in the teleport built-in changed. sco-core stays pinned at `17e2620`.
- The `npc` built-in is now a pure consumer of sco-core's `game.actors` (`sc_actors.h`) and `teleport.spatial`: `npc.spawn` spawns through `spawn_npc` about 3 m in front of you (no ground ray, so a slope can put an NPC slightly in or above the ground), `npc.clear` despawns the ids it spawned, and the game pack despawns them when the built-in unloads. The commands are gated on `game.actors.spawn_npc` and `game.actors.despawn`, an unknown archetype is refused by the game, `npc.clear` no longer removes crew NPCs (the tab's remove button still does), and the built-in has no tick of its own (`ProcessNpcs` keeps running for `npcs.txt`). sco-core is pinned at its main `a0eeee5` (`game.actors` 1.1 publishes `[game] game.actors state probe ...` lines when downed or dead).
- The `crew` built-in is a plain consumer of sco-core's game pack now: `crew.sit`, `crew.stand_all`, `crew.fill`, `crew.clear` and `crew.power_on` use `game.vehicles` (seats, seating, Flight Ready) and `game.actors` (the NPCs), and include only SDK headers. `crew.fill` takes only seats that are empty and usable (the game's own seat picker accepts them) and logs how many it skipped: `Seating 10 x <npc> (5 seats skipped: not interactable)`; the game pack's `mod.log` lists each seat as `interactable yes|no`. The commands act on the ship you're aboard, or the one `crew.target` pinned (no longer the last ship `spawn.ship` made), and `crew.clear` and `crew.stand_all` handle the NPCs the built-in added itself. The Crew tab still uses the old seat code and is unchanged. sco-core is pinned at its main `a0eeee5`.
- Build mode places, previews and removes props through sco-core's `game.entities` 1.0 (`sc_entities.h`) instead of the spawner: the preview follows your aim with `set_transform`, Undo and Clear base remove them (the preview is despawned). Prefabs (`.socpak`), NPCs and a game build where `game.entities` isn't ready still go through the spawner as before, and `spawn.entities` 1.2 callers see no change. Placed props stay in the world when the build built-in unloads or reloads: build mode calls `keep()` (game.entities 1.1) on each prop right after it spawns, never on the preview (which is still despawned), so a kept prop is no longer owned and Undo and Clear base remove it with the game's own entity removal (`RemoveEntityById`) as before; against a 1.0 game pack (no `keep`, logged once) props stay owned by the `build` plugin and go away if it unloads. The `spawn_probe` test plugin's Ctrl+Alt+= walks `game.entities` (spawn 5 m ahead, move 1 m up, read back, class, despawn). sco-core is pinned at `5424b97` (sco-core PR #72, the `keep` commit; repin to main once it merges).
- **Natural mining built-in (scaffold, off by default).** A new `mining` built-in (`src/builtins/mining_plugin.cpp`) hooks sco-core's `mining.cell` row (`CBiomeBuilder::BuildLargeScaleEcoSystem`, capability `game.mining`) and ORs the spawning bit `0x8` into the rule flags of ecosystem cells that built only their physics (`0x6` -> `0xE`), ported from the `junikka/sc-offline-mining` fork with its eligibility rules. New `sc-offline.ini` keys `mining = off` and `mining_debug = off` (`SC_OFFLINE_MINING`, `SC_OFFLINE_MINING_DEBUG`), the command `mining.status`, and `[mining/trace]` counters in `mod.log` with `mining_debug`. Not ported: the fork's executable-hash check and fixed RVAs (the row replaces them) and its server and editor context bytes. There are now eleven built-ins. Untested in game. sco-core is pinned at its `game-rows-mining` branch (#71).
- `spawn.entities` 1.2 is now published by sco-core's game pack under the owner `game`, with the same table, version, refusals and `[game] warning:` log lines (the game pack also publishes `game.actors` and `game.vehicles`); the `spawn` built-in keeps `spawn.ship`, the spawner's tick and the Vehicles tab, and ASOP registers your retrieved ship with the game pack's mover. Plugins see no difference. `mod.log` reads `[game] game services published: teleport.spatial 1.0, spawn.entities 1.2, game.actors 1.0, game.vehicles 1.0`. sco-core is pinned at its main `b0696a4`.
- `teleport.spatial` is now published by sco-core's game pack (game pack 0.1.0) under the owner `game`, with the same table and version; the `teleport` built-in keeps only its commands (`teleport.save`, `teleport.go`, F7 and F8). The spawn built-in's mover converts zones through the game pack's same zone tree. Plugins see no difference. sco-core is pinned at its game-services branch (#56).
- The launcher window is rewritten on Direct3D 11 and ImGui (the vendored ImGui the mod's menu uses), with a sidebar (Home, Plugins, Output), a status banner, a Discord switch, and Settings and Logs in the top bar. Every command, ini key and safety check of the old window and of the console run is unchanged. `sc-offline.exe` now links `imgui` and sco-core's vendored SQLite. Scaffold: compiled and linked with clang and mingw headers, not yet built by CI (MSVC) or run in game.
- A launcher **Plugins** page manages `data/plugins/<id>/` through each `plugin.ini`: it lists id, name, version and author, switches a plugin off or on with the `<id>/disabled` marker sco-core's discovery honours, and edits a plugin's declared `[settings]` into its `data/storage/<id>.db` (where `sco.settings` keeps them) while the game is closed. Its Reload button only explains that hot reload runs in the game (`sco.reload`). Plugins are off unless `plugins = on`; the page says so and can switch it on.
- Ship terminals list every ship in `ships.txt` by default: `asop_fleet_list` now defaults to `ships` (in the shipped `sc-offline.ini` and when an ini doesn't set it). With `game`, offline, a terminal showed no ships at all, because the game's own entitlement query fails. `asop_fleet_list = game` still selects the game's list.
- sco-core is pinned at its main `491e583`: sco-lua no longer leaks a task when `run_on_game_thread` runs out of memory, scripts share a 256 MiB memory cap on top of 64 MiB each, `sco.invoke` refuses a disabled script, and plugin `log`/`status` text stays on one line in `mod.log` (CR and LF become spaces); the host kit gains `Platform::registerSignatures` (sc-offline registers its game tables itself, so nothing changes in the game).
- **Multiplayer (LAN or VPN)**: a new **Multiplayer** tab hosts or joins a private session between players who all run sc-offline, on sco-core's `sco.net` (sco-core is pinned at its main with `sco.net` 1.0). Each player runs their own offline game; the others appear as ghosts your game spawns (an NPC, or the ship they're aboard) and moves about ten times a second, matched by zone name so they line up wherever both games have the zone loaded, and parked out of sight when they're somewhere you haven't loaded. Ghosts are visual only: no shared physics, damage, missions or inventory. Nothing connects until you press Host or Join; sessions need a passphrase and stay on your LAN, plus the ranges in the new `multiplayer_allow` setting for a VPN (empty by default). New `sc-offline.ini` key `multiplayer = on|off` (on). With `block_network` on, the firewall rule for `StarCitizen.exe` now leaves the LAN (and `multiplayer_allow`) out, and a new rule lets your subnet reach the game over UDP for a session you host; both are undone when the game closes like the others, and `multiplayer = off` keeps the old block-everything rule. Commands `multiplayer.status`, `multiplayer.leave` and `multiplayer.goto <player>`. `mod.log`'s `[plugin]` report now lists ten built-ins. The game's own netcode and servers aren't used or changed. See `docs/features.md#multiplayer-lan-or-vpn`.
- The bridges' channel layouts now come from sco-core's SDK: `titanlink` and `voxel_bridge` (and `bridge-peer`) include the MIT headers `sc_titanlink.h` and `sc_voxel_bridge.h`, and the local `titanlink_wire.h` and `voxel_wire.h` are gone. The layouts are byte for byte the same (layout version 1), so nothing changes in game and a peer built against the old headers still attaches. The Titanfall 2 side is a separate Northstar plugin in its own repository, built against the SDK alone ([docs/bridges.md](docs/bridges.md)). The release build still contains no bridge code. sco-core is pinned at the commit of its SDK branch that adds the headers (it also brings sco-core's #42 and #44), to be repinned to the merge commit once that lands.
- F6 enters build mode without opening the Build tab first: it loads the build list itself (it used to answer "build mode isn't available yet" until the tab had been opened once), and when it still can't, `mod.log` says why. When `set_entity_transform` answers 0, `mod.log` now says why, once per entity and reason: not yours, not streamed in yet (a fresh spawn takes seconds; wait for `entity_alive`), zone conversion failed, or a bad argument. The `spawn_probe` test plugin's Ctrl+Alt+2 waits for its ship to stream in before moving it.
- Two optional bridges, **not in the release**: `titanlink` (Titanfall 2 through Northstar: pilot mode on F9, the match drawn from your eyes in an overlay, V calls your Titan, E gets in) and `voxel_bridge` (a voxel game's solid blocks become crates in a building area, and it gets Star Citizen's ground back; Ctrl+F9). They're built-ins compiled in only with the CMake options `SCO_BRIDGE_TITANLINK` / `SCO_BRIDGE_VOXEL`, off by default; CI builds them in a job of its own (artifact `bridges-test`, with `bridge-peer.exe` to stand in for the other game) and checks the release's `dinput8.dll` has neither. Each talks to the other game through sco-core's `sco.ipc` (`Local\SCO_titanlink.link`, `Local\SCO_voxel_bridge.link`, the `sc_ipc.h` wire, current user only) and checks everything the other side writes. The other games' halves aren't in this repository; `docs/bridges.md` describes the wire they speak and what isn't ported yet (keyboard takeover, hiding your body, Titanfall-style movement, drawing the voxel world). Nothing changes in the release build.
- **Ship terminals, personal hangars, the hangar lift and ATC work offline.** At a station's ship terminal, Deliver stores a ship there and Retrieve gets you a hangar from the ATC: the elevators take you to it, and the hangar's lift brings the ship up to the floor. Store at the hangar terminal takes it back down. Hailing ATC from your hangar opens the doors for take-off, and a landing hail from outside shows your pad. One ship can be out at a time, and stored ships last for the session. Each part switches itself off when the game's code for it isn't found, with one `[+]` or `[!]` line per part in `mod.log` (`[asop]`, `[iim]` and `[atc]` lines log each step). `asop = off` in `sc-offline.ini` turns it all off. Your retrieved ship is registered with the spawn built-in, so plugins can move it with `set_entity_transform`. The terminal only offers Deliver with the game's delivery system switched on, which online the server does: sc-offline sets `g_itemRecovery.deliverySystemSetup` to 1 (`[asop] g_itemRecovery.deliverySystemSetup 0 -> 1 set` in `mod.log`). The ships listed from `ships.txt` now carry a valid entitlement URN (`urn:sc:platform:entitlement:uuid:...`); the old one was the game's "unset" URN, so the terminal never offered Deliver, Retrieve or Claim on any row. `asop_fleet_list = game | ships` picks the terminals' ship list: the game's own (the default; offline it's empty) or every ship in `ships.txt`. While a terminal is open, `mod.log` shows what its screen is bound to (`[asop] terminal: ...` and `[asop] terminal row ...` lines: the selected row and its Deliver, Retrieve and Claim flags). sco-core's signature rows are now resolved before the offline patches, because the ASOP rows match the game's original bytes. The fleet manager's own retrieve no longer asks the ATC directly while this Retrieve is on. sco-core is pinned at its main with the ASOP rows (d76ce54).
- Plugins can move what they spawn: the `spawn.entities` service is now 1.2, and its header ships in sco-core's SDK as `sc_spawn.h` (`src/builtins/spawn_service.h` is gone). `set_entity_transform` moves and turns an entity to a position and rotation in the world frame or in a zone's frame. A plugin may move only entities it spawned with the new `spawn_as` (what `spawn_near_player` and `spawn.ship` spawn belongs to nobody), and forgets them when it unloads; your own ATC-retrieved vehicle joins that list once it's registered (a later change). Build mode's preview, NPC removal and the mover share one move function. sco-core is pinned at its `sc_spawn.h` branch until that merges. The `spawn_probe` test plugin's Ctrl+Alt+2 now walks the mover through three steps.
- The `teleport.spatial` header now ships in sco-core's SDK as `sc_spatial.h`, so plugins include it from the SDK instead of copying `src/builtins/spatial_service.h`, which is gone; the table is unchanged. CI now fails if `src/third_party/imgui/` (`imconfig.h`, `imgui.h`, `LICENSE.txt`) differs from the ImGui headers the SDK ships. Nothing changes in game. sco-core is pinned at its SDK branch with `sc_spatial.h`.
- Your saves move into sco-core's `sco.storage`: the F7 / F8 spot, the Travel tab's saved spots and your wallet are kept by the `teleport`, `quantum` and `contracts` built-ins in `data\storage\teleport.db`, `quantum.db` and `contracts.db` (SQLite; a save is all or nothing, so a crash can't leave half of one). Nothing changes in game. On the first start `spawn.txt`, `bookmarks.txt` and `wallet.txt` are imported (`[storage] imported spawn.txt into teleport` and so on in `mod.log`) and left in place. `wallet.txt` is still written and can still be edited by hand: a `wallet.txt` changed since the mod wrote it wins, and deleting it still takes you back to the starting amount. If storage can't be used, each feature says so in `mod.log` and uses its `.txt` file as before. See `docs/data-files.md#saves`. sco-core is pinned at its main with storage, the UI registry and DataCore AddRecord.
- The menu is now a shell over sco-core's `sco.ui` service. Each built-in draws its own tabs (Player and Squadron 42 from `loadout`, Travel from `quantum`, Vehicles from `spawn`, Crew, NPCs and Build), and F6, F7 and F8 are the `build` and `teleport` built-ins' hotkeys. The tabs, their order and the keys are the same as before. Plugins can add their own tab with a badge, an overlay and hotkeys (`ctrl+alt+9` and similar); see `docs/plugin-ui.md`. A fault while a plugin's tab is drawn switches off only that plugin. Every key press bound through `sco.ui` is logged as `[hotkey] F7 -> teleport.save: OK ...`. sco-core is pinned at its main with `sco.ui`.
- The Gladius' new quantum drive no longer depends on one exact game version. Its game data is now a data pack, `data/builtin/quantum/datacore/quantum_drive.toml`, which names the records and fields it changes instead of byte offsets in one `Game2.dcb`; sco-core's pack loader applies it as the game loads its data. The old byte patch was made for 4.10.193 and switched itself off on 4.10.196; the pack applies to 4.10.196's game data too. `mod.log` reads `[+] new quantum drive: game data patched as it loaded (pack quantum_drive: 508 operations), load ok in N ms`, or, when a game update removed something the pack uses, names the record or field that is missing. sco-core is pinned at its `sdk-v1.1.0` release (5e3d463).
- The rest of sc-offline's features are built-in plugins now, after teleport and spawn: `crew`, `loadout`, `npc`, `ammo`, `quantum`, `build` and `contracts`. They work as before, from the menu and their keys (F6 for build mode). Each runs its per-tick work from its own tick subscription, so a fault in one switches that feature off instead of crashing the game, and each gives plugins commands (`crew.sit`, `npc.spawn`, `loadout.equip`, `ammo.infinite`, `quantum.travel`, `build.toggle`, `contracts.status` and more; see `docs/features.md`) gated on a capability of the same name. `mod.log`'s `[plugin]` report lists nine built-ins (`9 found, 9 loaded` with `plugins = off`), and on quit they unload after every plugin. A plugin folder named after any of them is refused.

- Plugins can ask where things are: the teleport built-in publishes a `teleport.spatial` service (`src/builtins/spatial_service.h`) with your position and orientation, the zone an entity is in, zone names, and positions converted between zones and the world. It runs on sco-core's zone tree, fed from the game every tick. `spawn.entities` is now 1.1 with `entity_alive(id)`; plugins built against 1.0 keep working. Nothing changes in game. sco-core is pinned at its main with the engine math and zone tree.
- The ship spawner is now a built-in plugin, `spawn`, the second after teleport. Spawning works as before. Plugins get a `spawn.ship <class> <height>` command and a `spawn.entities` service (`src/builtins/spawn_service.h`) to spawn entities near you directly, and a fault in the spawner's per-tick work now switches the spawner off instead of crashing the game. `mod.log`'s `[plugin]` report lists `spawn <version> builtin loaded`, and with `plugins = off` it reads `2 found, 2 loaded`. A plugin folder named `teleport` or `spawn` is refused, whatever its kind.
- Every detour (about 35 of them) now goes through sco-core's `sco/hook.h` instead of sc-offline's own cave allocator; nothing changes in game. A detour that can't be placed logs `[!] detour at 0x...: <reason>`. sco-core is pinned at its API 1.1.
- Plugins now get `game.exit` and are unloaded when you quit from the game's menu. The game ends itself there without the usual Windows quit message, so they never did before; sc-offline now runs the shutdown from the game's own Quit. `mod.log` shows `[app] game quit hook: installed` at startup and `[app] game closing (CSystem::Quit): ...` on quit. A crash or a killed game still sends no `game.exit`.
- The build is now CMake instead of a Visual Studio solution: open the folder in Visual Studio 2026, or `cmake -S . -B build -A x64` and `cmake --build build --config Release`. `sc-offline.slnx` and the `.vcxproj` files are gone; nothing changes for players. See `docs/build.md`.
- sc-offline now runs on [sco-core](https://github.com/scubamount/sco-core)'s host kit (a git submodule in `external/sco-core`: clone with `--recurse-submodules`). The features work as before. `mod.log` gains a `[core] signatures: N/N OK` line and a `[plugin]` report, and status-strip messages are logged as `[status]` instead of `[ship]`.
- F7 and F8 are now the `teleport.save` and `teleport.go` commands of a built-in `teleport` plugin, the first of sc-offline's features on sco-core's plugin API; plugins can call them too. They work as before. `mod.log`'s `[plugin]` report lists `teleport <version> builtin loaded`, with plugins on or off.
- The plugin system no longer depends on teleport: on a game build where teleport's addresses aren't found, the host kit and plugins still start.
- **Plugins**, off by default: with `plugins = on` in `sc-offline.ini`, plugins in `data\plugins\<id>\` built with the sco SDK (native, Lua or data packs) are loaded and listed in `mod.log`. See `docs/features.md`.

## 0.7.0 (2026-10-07)

- **Safer self-update** ([#31](https://github.com/scubamount/sc-offline/issues/31)). Each release zip now carries `manifest.json`: the version, tag, commit and every shipped file with its SHA-256. CI writes it and checks the finished zip against it. The launcher only installs files the manifest lists, with matching hashes, from a release newer than itself; paths with `..` are refused. Downloading and checking run with normal rights; for a Program Files install only the file swap asks for administrator rights, with no network. Each file is flushed to disk and checked again after it's moved; files locked by antivirus are retried. The update waits while the game runs, checks free disk space, and only times out on a stalled download. The new launcher must pass `--self-test` or the old files go back. New setting `update_channel = stable | prerelease`. `docs/launcher.md` explains how to check a download by hand.
- **Discord status** ([#34](https://github.com/scubamount/sc-offline/issues/34)). While the game runs, your Discord profile shows **Playing sc-offline** with the version, time played and buttons to the sc-offline Discord and download page. It talks only to the Discord app on your PC. Turn it off with the **Show on Discord** box in the window or `discord_presence = off`.
- Project activity (releases, merged PRs, issues opened or closed) now posts to the project's Discord channel ([#30](https://github.com/scubamount/sc-offline/issues/30)).
- Docs and `sc-offline.ini`: `start_ship` only applies with `start = Daymar`, the wallet lives in `data\wallet.txt`, and a new **Not available offline** list in `docs/features.md` covers ASOP, the vehicle manager and character creation ([#26](https://github.com/scubamount/sc-offline/issues/26)).
- README: 0.6.1 has been played in game on Windows; the "nobody has played it" note is gone. Linux through Wine is still untested.

## 0.7.7 (2026-10-11) — EXPERIMENTAL HOTFIX

For Star Citizen 4.10.2.

- Fixes competing death-hook ownership: the game pack owns the detour, captures actor ID before original code under SEH and forwards all three original arguments. `game.actors.died` covers every actor; `game.player.died` remains local-player only. C/C++/C#/Lua adapters carry the 24-byte event payload.
- Contracts subscribe under their built-in owner with cleanup on unload/failure, ignore unknown ID 0 and preserve the 32-entry recent-death ring. The entity-only fallback logs a degraded-bookkeeping warning once. Core is pinned to `2197dfbf5a630e5a16a201526e628f1d28df908a`.
- Startup megamap crash #109 remains under investigation. Built/tested in Windows CI; not run in game when this entry was written. Maintainer elects to ship and triage player reports. This hook repair does not prove a startup-crash cure.
- Later, per the [v0.7.7 release notes](https://github.com/scubamount/sc-offline/releases/tag/v0.7.7): the maintainer ran v0.7.7 in game once with NVIDIA Smooth Motion off, and Smooth Motion is named as the cause of the startup crash in #109 and #112. That run is not a per-feature check.
- Crash diagnostics report the fault module/offset and raw-stack executable-address candidates, not an unwound call chain; these candidates do not establish causation.

## 0.7.6 (2026-10-11) — EXPERIMENTAL

For Star Citizen 4.10.2.

- **Launch-token fallback and diagnostics.** `play` attempts to launch with the desktop's normal-rights token even from an elevated launcher. When that token is unavailable (for example with UAC disabled), it falls back to the existing launch path and warns. An executable configured to require administrator rights needs that setting removed. This behavior does not establish an OBS/elevation cause or a megamap crash cure.
- **`mod.log` names what crashed.** The game's crash line in `Game.log` gives only an address. sc-offline now adds `[crash]` lines to `mod.log` with the module and offset that faulted (the game, a driver, an overlay) and raw-stack executable-address candidates (not an unwound call chain), then lets the game's own crash handler run as before. `mod.log` also warns when the game runs as administrator.

## 0.7.5 (2026-10-10) — EXPERIMENTAL

For Star Citizen 4.10.2.

- **Your Discord status shows the version you're actually running.** It used to be part of the launcher, with the version fixed into the launcher when it was built, so a player whose Discord showed an older sc-offline saw the wrong number. It is a plugin now, `data/plugins/discord` (`plugins/discord/`, built only from the sco SDK's headers like `creative`): the version comes from the running mod (`api->host_version()`), and the status also shows where you are (the moon, planet or station) and the ship you're aboard. Its options (where you are, your ship, the buttons) are on its page in the game's menu. The **Discord** switch in the launcher's top bar and the **Plugins** page turn it on and off; an old `discord_presence = off` is carried over once and the line removed. The launcher's own Discord code is gone.
- **Plugins load by default** (`plugins = on` in the shipped `sc-offline.ini`), so the Discord plugin runs out of the box. `creative` still ships switched off. An existing `sc-offline.ini` keeps whatever `plugins` line it has.
- `tools/check.sh` now also parses `plugins/*/*.cpp`.

## 0.7.4 (2026-10-10) — EXPERIMENTAL

For Star Citizen 4.10.2.

- **Leaving the game makes the PC safe to go online right away.** After you quit from the game's own menu, sc-offline used to keep the mod in `Bin64`, the firewall rules and the renamed Easy Anti-Cheat in place until you answered its questions about a crash report and the session's logs; until then the window stayed red and every button, Uninstall included, was greyed out. Now the mod is taken out and the PC changes are undone as soon as the game closes, the window says so, and only then come the questions.
- **`sc-offline.exe` and `dinput8.dll` now carry version information** (Properties > Details: product sc-offline, version 0.7.4, description, GitHub address). Files without it are a common reason antivirus heuristics treat a new program as suspicious. Some players' Microsoft Defender flags the 0.7.x downloads as `Trojan:Script/Wacatac.H!ml`; that name is a machine-learning guess, not a known virus. The release zip is built by GitHub Actions from this repository's source, and every file's SHA-256 is in `manifest.json` (see `docs/launcher.md`, Checking a release by hand).

## 0.7.3 (2026-10-10) — EXPERIMENTAL

- **The menu no longer slows the game to a crawl.** The menu used to wait on the game for every frame it drew; now it never waits. While the game is building the menu's next picture, the menu keeps answering its window and the M key and shows its last picture, so a busy game can't stall the menu and the menu can't stall the game. Keeps 0.7.2's changes: the cursor is clamped only when the menu window moves, and the menu stops asking the game for pictures while it isn't focused.
- The 0.7.2 `[menu] perf:` diagnostic line is gone.

## 0.7.2 (2026-10-10) — EXPERIMENTAL

- **Fixes the menu making the game stutter to a near-standstill** while the menu is open and focused, with the game running normally again the moment the menu loses focus. Two changes: the cursor is now clamped to the menu window only when the window actually moves or resizes instead of on every frame (clamping the cursor is a synchronous, cross-process call, and doing it every frame while the menu has focus was enough to starve the game's own thread); and while the menu is visible but does not have focus the menu freezes its picture and stops asking the game thread to rebuild it, so the game runs at full speed until you focus the menu again. The frame is still built on the game thread, as before — that keeps a plugin's drawing inside the game's crash guard — this only stops doing it while the menu isn't focused.
- **Menu now logs a one-line-per-second performance summary** (`[menu] perf:` fps, build-average and build-max milliseconds, and how many frames per second timed out and were skipped) while the menu is open. If the game is still slow with the menu open, this line says where the time goes: a build cost that climbs over a few seconds points at a tab's drawing, a low fps with a flat, tiny build cost points at the interrupt itself. Leave the menu open for a few seconds, then send back the `[menu] perf:` lines from `mod.log` — they are what a follow-up fix will need.
- **Still experimental:** like 0.7.1, this has not been played in game. It fixes a specific reported slowdown and carries the probe that proves (or disproves) it did; runtime paths are unproven until someone runs it.

## 0.7.1 (2026-10-10) — EXPERIMENTAL

- **Supports the 2026-10-07 Star Citizen build.** The mod resolves every game address at run time by scanning the executable's byte patterns, so a game patch that moves code no longer breaks it: the signature scanner re-finds the functions on the new `StarCitizen.exe` instead of trusting fixed offsets. All 103 pattern call sites (34 `FindUniquePattern`, 5 `FindPattern` and 64 `FindCString`) resolve against the 2026-10-07 build; the same scan passes on the 4.10.196 build that 0.7.0 shipped against, so nothing was pinned to one version. Nothing else changes: the features, the menu and the settings are the same as 0.7.0.
- **This release is experimental:** it has not been played in game on the new build. It is a compatibility rebuild, not a feature change — the addresses were verified by scanning the new build's executable and by a clean compile and link, but every runtime path is unproven until someone runs it. If the menu doesn't open or the game crashes, send back the matching lines from `mod.log` and `Game.log`.

## 0.6.1 (2026-10-07)

- **Online-safe light now updates while you play** ([#22](https://github.com/scubamount/sc-offline/issues/22)). It used to freeze while **Play** was running, so it stayed green with the game open and only changed at the end. It now refreshes every 1.5 seconds and turns red as soon as the mod is copied in or `StarCitizen.exe` is running.
- **Wider window** (1160 px instead of 820 px), so log lines wrap less, and a **SCUBAMOUNT** watermark.
- `--window` opens the window from a terminal or under Wine/Proton.
- The `data` folder is created on first run, so a fresh unzip no longer warns that the folder can't be written.

## 0.6.0 (2026-10-07)

- **Launcher window** ([#20](https://github.com/scubamount/sc-offline/issues/20)). Double-clicking `sc-offline.exe` opens a window with **Play**, **Status**, **Update**, **Install**, **Uninstall**, **Open settings** and **Open logs**. Each button runs the same CLI command, its output shows in the window, and its questions get Yes/No buttons. An **Online-safe** light is green when the mod is out of Bin64 and no PC changes are left, red with a list otherwise; **Uninstall** is enabled only when there is something to undo. Every CLI command works as before; `--console` keeps the old double-click behaviour.

## 0.5.1 (2026-10-07)

- **Firewall also blocks the RSI Launcher and CIG's `CrashHandler.exe`** while you play ([#17](https://github.com/scubamount/sc-offline/issues/17)), recorded and removed with the existing rule. `sc-offline.exe` stays online for updates.
- **Crash reports** ([#18](https://github.com/scubamount/sc-offline/issues/18)): after a crash, offer a zip of the logs in `data\crash-reports` with the RSI handle, GEID/account numbers and Windows user name redacted, and a prefilled bug form. Nothing is uploaded; `.dmp` files are left out. New setting `crash_reports = on | off`.
- **Administrator rights only where needed** ([#16](https://github.com/scubamount/sc-offline/issues/16)): self-update of a protected folder and deleting protected session logs now ask Windows once, for that step only. The launcher and the game still run with normal rights. Warns when `data` can't be written.

## 0.5.0 (2026-10-07)

- **The launcher updates itself** ([#14](https://github.com/scubamount/sc-offline/issues/14)). On `play` and `status` it checks GitHub's latest full release (3-second timeout, never blocks play) and asks before installing. It downloads the release zip, checks it against GitHub's SHA-256 digest, swaps the program files with a journal in `data\update\applied.txt` (rolled back on failure, or on the next run after a crash), keeps `sc-offline.ini` and your saves, appends new ini settings commented out, and restarts itself. New command `sc-offline.exe update`, new setting `check_updates = on | off`.

## 0.4.2 (2026-10-07)

- **Offer to delete this session's game logs** ([#12](https://github.com/scubamount/sc-offline/issues/12)). When the game closes, the launcher lists the `Game.log`, `logbackups` and `Crashes` files written during the session and asks "Delete these files?" then "Are you sure?"; anything but `y` keeps them. Older logs are never touched. New `sc-offline.ini` setting `clean_logs = ask | off`.
- The leftover-changes prompt now reads a whole line, so its Enter no longer answers the next question.

## 0.4.1 (2026-10-07)

- **The launcher finds the game in more places.** After `game =`, it now tries the folder it found last time (`data\game-path.txt`), where the RSI Launcher says the game is (its install entry and the paths in `%APPDATA%\rsilauncher`), more usual folders (`Game\Star Citizen\StarCitizen`, `Games\StarCitizen` and similar), a four-level search of every fixed drive, and finally a folder picker. `game =` also accepts the folder that holds `StarCitizen`.

## 0.4.0 (2026-10-07)

- **The launcher sets up offline play itself.** Each play, its helper blocks `StarCitizen.exe` in Windows Firewall, adds the EAC hosts line and renames `EasyAntiCheat_EOS.exe`, then undoes exactly those changes when the game closes. New `sc-offline.ini` switches `block_network`, `eac_hosts`, `eac_rename` (all on). Changes are recorded in `%ProgramData%\sc-offline\pc-changes.txt`; after a crash, `status` lists them, `uninstall` undoes them and `play` offers to.
- The manual EAC steps are gone from the README's Setup; Windows now asks for administrator rights once per play.

## 0.3.0 (2026-10-07)

- **Renamed to sc-offline.** The original ChrisWareOffline project has shut down; this is now an independent project based on ChrisWareOffline 0.9.0-rc1 (GPL-3.0). Window title, menu title, `mod.log` and the launcher say sc-offline. Removed the original project's Discord links.
- One version number for the DLL and launcher (`SCO_VERSION` in `src/version.h`), replacing `0.9.0-rc1 / sc-offline …`. The launcher still recognizes older builds when checking or uninstalling.
- Removed the original author's prebuilt `dinput8.dll` from the repository root. Its source was never published and no release used it.
- Solution and project renamed: `sc-offline.slnx`, `src/sc-offline-dll.vcxproj`.
- Bug reports now go to GitHub Issues, with a template asking for the logs.

## 0.2.0-rc5 (2026-10-07)

- Removed `data/scripts/` (229 Star Citizen Subsumption mission XML files) and rewrote the repository history so no commit contains them. They are CIG's content and are not ours to distribute.
- `contract_scripts.txt` now lists only the 796 contracts that need no mission script; contracts that depended on the removed scripts are no longer offered.
- Earlier release zips (rc2 to rc4) contained those files and were withdrawn.

## sc-offline 0.2.0-rc4 — 2026-10-06

Second parity pass against the original author's DLL (REA 4.1.0 + Ghidra), a launcher with
subcommands and self-checks, and the documentation split into a short README plus `docs/`.
Release candidate: compiled and checked, not yet run in game.

### Squadron 42 tab
- **Spawn** lists the whole `[sq42]` group again. The search box started as `sq42`, which matched
  item names and hid 59 of the 60 entries; it now starts empty and searches within the group.
- **Ships** are greyed out when this game build doesn't have the class, instead of failing after
  the click. Your own ships spawn 30 m up by default, like the original (was 20 m).
- **Settings** checkboxes are greyed until the game's current value has been read.
- **Console** has a **Run** button and is disabled until the game's console is found; commands
  can be 255 characters (was 191).

### Ships and seats
- Sitting in a pilot seat with Flight Ready off now says "Press R (Flight Ready) to power up."
- `mod.log` lists every seat of a spawned ship once (`[ship] N seats: name(priority, taken)`),
  the names "Board in a seat by name" matches against.

### Build
- The reach tooltip says where objects land when you look at the sky: under the point that far out.

### Launcher
- Commands: `sc-offline.exe [play|install|uninstall|status|help]`, plus `--dry-run` (print every
  step, change nothing) and `--skip-eac-check`. A double-click is `play`, as before.
- Self-checks on every run: which `dinput8.dll` it is (source build version, or the original
  author's prebuilt by SHA-256, with a `boot_map = PU` warning for the prebuilt), whether the game
  updated since the last play (`build_manifest.id`), a mod left in the game folder after a crash,
  and Easy Anti-Cheat (`EasyAntiCheat_EOS.exe` present, hosts-file block). An active EAC stops
  `play` and `install` with the fix printed.
- `default_1.xml` is backed up before the launcher replaces it and restored afterwards (it used
  to be overwritten and left behind). Another mod's `dinput8.dll` is set aside and put back.
- `Bin64\sc-offline.installed` records what was installed, so `uninstall` knows what to undo.
- Everything printed also goes to `data\launcher.log`. Exit codes: 0 ok, 1 error, 2 EAC active,
  3 the game is running.
- `sc-offline.sh` takes the same commands and also finds Lutris and Steam/Proton prefixes
  (Flatpak Steam included) and their Wine, checks `/etc/hosts`, and `--dry-run` starts nothing.
  `--prefix` / `--wine` override the guesses.

### Repository
- Documentation: a short README for players; features, data files, launcher, Linux and build
  notes moved to `docs/`. Stale release history is gone; corrected: the download is about
  1.4 MB, not 1 GB, and of 2153 listed contracts 1657 have their scripts shipped and 491 are offered.
- `data/missions.txt` removed: no build ever read it.
- CI: dead NuGet steps removed; runner images pinned (`windows-2025-vs2026`, `ubuntu-24.04`); a
  newer push cancels the older branch build. The release zip now carries `docs/`.
- `.gitattributes` fixes line endings per file type (`sc-offline.sh` stays LF on Windows clones).
- `tools/check.sh` screens `launcher/` for MSVC C2712 too.
- CHANGELOG entries carry their release dates.

## sc-offline 0.2.0-rc3 — 2026-10-06

Brings ours in line with the original author's prebuilt DLL, based on a Ghidra decompile of it.
Release candidate: compiled and checked, not yet run in game.

### Outfits
- Outfits that don't name a head (all the Navy, Marine, bridge, deck and medic ones) get the
  default face instead of stripping the head. Named heads get eyes and teeth when the outfit
  lists none.
- Hats, eye and head accessories, Vanduul horns and jewellery go on the head.
- The belt / vest layer (`Clothing_Torso2`) sits inside `Clothing_Torso_1`, and clothing no
  longer nests inside armor. An undersuit is added when an outfit names armor but no undersuit.
- Unknown item names in `outfits.txt` are skipped at load, and outfits left empty are dropped;
  `mod.log` counts both.
- Picking an outfit only selects it; **Wear SQ42 outfit** wears the selection. The built-in
  `sq42_pilot_*` preset is gone (none of its item names exist in the original DLL).
- The **SQ42 visor HUD** checkbox now applies to the gear menu's **Equip** too, and shows
  before the outfit list has loaded.

### Menu
- Typing in the ship search box selects the first matching ship, so a filtered-out ship can't
  be spawned by accident.
- The Squadron 42 spoiler warning has **OK** and **Back**; Back returns to the first tab.
- The Bengal rows are labelled for what they do: **Bengal (UEE)** and **Bengal + Vanduul wing**.

### Missions
- The AI debug-nodes console command uses the original's name `SubsumptionEnableDebugNodes`
  unless only the `ai_` form exists in this game build.

## sc-offline 0.2.0-rc2 — 2026-10-05

The launcher release. Same mod as 0.2.0-rc1; how you start it changed.
Release candidate: compiled and checked, not yet run on Windows or Linux.

### Launcher
- `sc-offline.exe` replaces `launch_offline.bat`. Same steps (copy the mod in, start the game,
  remove the mod when every `StarCitizen.exe` has exited), plus:
  - finds the game itself: `Roberts Space Industries\StarCitizen\<channel>\Bin64` on every
    fixed drive, or `game =` in `sc-offline.ini`, or `--game <folder>`;
  - copies and removes the mod through a separate helper process, so closing the launcher
    window early still takes the mod out once the game exits; the helper alone is elevated
    (one UAC prompt) when the game folder needs it, and the game never runs as administrator;
  - reads the start ship, start location, boot map and channel from `sc-offline.ini`.
- The default boot map is now `PU_All` (every star system, so Travel reaches Pyro and Nyx).
  Set `boot_map = PU` when playing the repo-root prebuilt DLL.
- Releases ship `sc-offline-<tag>.zip`: launcher, DLL, `sc-offline.ini`, `sc-offline.sh`, `data/`
  and the docs, ready to extract and play.

### Linux (experimental, untested)
- `sc-offline.sh` runs the launcher inside a Star Citizen Wine prefix (LUG Helper layout),
  with `WINEDLLOVERRIDES=dinput8=n,b` so Wine loads the mod instead of its own `dinput8`.

## sc-offline 0.2.0-rc1 — 2026-10-05

This repository's build of upstream 0.9.0-rc1 (below) with the Squadron 42 tab on top.
Release candidate: compiled and string-checked, not yet played.

### Squadron 42 tab
- An eighth tab, between Build and Menu: outfits, the SQ42 pilot preset, four SQ42 settings,
  a one-shot buildable spawner, SQ42 ships, Bengal A / B with the Vanduul wing, and a console.
- Ships from this tab use the Vehicles tab's seat rules and become the Crew tab's target ship.

### From sc-offline 0.1.x, carried forward
- The fleet manager offers all 1102 ships in `ships.txt` (it stopped at 1024).
- The gear menu and the outfits share one loadout loader and one temp-file counter.
- `tools/check.sh` checks the source on macOS / Linux before CI does.

## 0.9.0-rc1

First release candidate of the seats, crew, travel and menu update. Debugging and analysis code
from development is removed, and the release build is optimized.

### Menu
- New tabbed menu: Player, Travel, Vehicles, Crew, NPCs, Build and Menu.
- Dark green theme, Bahnschrift font (Segoe UI fallback), and a status line at the bottom that
  shows what the mod just did.
- Optional background image: save `data\menu_background.png` (or `.jpg`). Darkness and image
  position are set in the Menu tab. The file is ignored by git.
- The version shows in the title bar, the Menu tab and the first line of `mod.log`.

### Ships and seats
- Choose where you board a spawned ship: pilot seat, a seat by name, pick after it spawns, or none.
- Optionally remove the NPC in the seat you want instead of taking another seat.
- Crew tab: every seat on the ship and who is in it (you, NPC, empty). Sit here, Stand up,
  Remove NPC and Add NPC per seat; Fill empty seats, All NPCs stand up and Remove all NPCs.
- Power on now sends the game's own Flight Ready event to the pilot dashboard, including on ships
  whose dashboard is a separate part (F8C, Moth...). It falls back to pressing R only if needed.
- Infinite ship ammo: refills the magazines of the ship you're in. Every magazine on the ship is
  also topped up twice a second, for energy weapons that drain another way.

### Travel
- Travel tab with places grouped by star system: planets, moons, Lagrange points, comm arrays,
  jump points and landing zones, for Stanton, Pyro and Nyx.
- Scan the game for places finds everything loaded in a few seconds. Interiors and small zones are
  hidden unless you ask for them.
- Named saved spots, grouped by system, stored in `data\bookmarks.txt`. F7/F8 still work.
- Teleports refuse to cross star systems instead of leaving you stuck in empty space.

### NPCs and building
- Removing NPCs, kicking crew, build mode's undo and Clear base now work. The game wants an entity
  handle, not an id. NPCs the game refuses to delete offline are taken out of their seat and moved
  far out of range instead.
- Build mode previews prefabs with a flag while you move the camera and the real building when you
  hold still. Clicking keeps the previewed building.

### Build
- Release x64 is built with full optimization, link-time code generation and the static C runtime,
  so the DLL runs without the Visual C++ redistributable.

### Needs testing before the final release
These were built after the last in-game test and haven't been confirmed yet:
- The energy weapon top-up (does an energy weapon still run dry with infinite ship ammo on?).
- Proper NPC deletion through the entity system's handle lookup. If it doesn't take, the
  send-far-away fallback (confirmed working) still removes them from the world.
- Stand up and All NPCs stand up.
- The real-building prefab preview in build mode.
- Pyro and Nyx system names after a fresh place scan.

### Known limitations
- NPCs added to seats sit there but don't fly the ship or operate turrets.
- Power on uses Flight Ready, so some systems (engines, weapons) may start off on some ships.
  Per-system power options are planned.
- Places in Pyro arrive in orbit until their radii are added to `data\locations.txt`.
