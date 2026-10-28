# TitanLink: independent optional SDK consumer

GPL-3.0-only. Trusted native code runs with the host process's rights. This folder
is disabled by its `disabled` marker and is **not part of the normal release**.
It has independent version 0.1.0 and only the three documented native exports.
No private product or game-pack headers/symbols are linked. Host registration,
source removal and optional packaging composition are a separate integration step.

## Build, check, install

1. Obtain a checksum-verified public SDK containing `sco.ipc` 1.0,
   `teleport.spatial` 1.0 and **`game.spatial` 1.0 / `pose_to_zone`**. ABI is 1.1.
   This migration was tested with a local `integ/r1` legacy SDK archive. The pinned
published sdk-v2.4.0-rc.2 ships `sco_ipc.h` and `sc_game_spatial.h` (with
   `pose_to_zone`); no titanlink proof against that archive is claimed here.
2. Configure this folder **independently**, not inside the host CMake build:

   ```sh
   cmake -S plugins/titanlink -B titanlink-build -DSCO_SDK_INCLUDE_DIR=/path/to/verified-sdk/include -DSCO_WORKFLOW_TESTS=ON
   cmake --build titanlink-build --target titanlink-module-test
   ctest --test-dir titanlink-build -R '^titanlink-module-workflow$' --output-on-failure
   python3 tools/check_titanlink_sdk.py --archive /path/to/sdk.zip --sums /path/to/SHA256SUMS --work /path/to/new-proof-directory
   ```

   Windows uses MSVC `/W4 /WX`; POSIX development modules are not game-loadable
   DLLs. The checker copies this folder outside the host, verifies all archive
   checksums, traces all four module translation units including system-marked
   transitive headers, and rejects private headers and unresolved private symbols.
3. Install with `cmake --install titanlink-build --component titanlink --prefix /path/to/stage`.
   The folder contains module + plugin.ini + disabled. Keep it disabled until
   owner-run offline acceptance; enabling requires deliberately removing that
   marker in the installed folder and host plugin-loading consent. Do not enable
   or add it to a normal release automatically. Use matching folder/manifest ID.

## Retained commands and behavior

All original IDs remain: `titanlink.pilot`, `titanlink.titan`,
`titanlink.embark`, `titanlink.unlink`, `titanlink.status`. Missing service tails,
null callbacks or failed lookups refuse explicitly; there is no private fallback.
Status works with absent providers. No service is created at plugin load.

- Pilot request/F9 opens only the owner-scoped `link` IPC channel. The matching
  MIT wire remains `sc_titanlink.h` v1: layout 0x314B4C54, 64 KiB, original block
  and ring offsets. Once linked, one validated map/mode match request is sent.
- Feet anchor and body-facing rotation, 1.62 m eye height, velocity, clamped FOV,
  view size and held buttons are published on tick (expected host 10 Hz).
  Cross-zone positions **and rotations** use public G3 `pose_to_zone`, never
  three positional conversions or private teleport memory.
- Titan call is 8 m ahead, limited to one per 8 s. Embark requires a parked Titan
  strictly within 9 m; disembark is a typed peer message. These never move the
  Star Citizen player. Busy/in-Titan and unknown-position refusals are retained.
- Peer flags/coordinates/counts/text are bounded; malformed state stops pilot
  mode rather than reusing old state. Malformed frames are rejected, short/torn
  frames marked unavailable. Corrupt message rings latch off until reopening.
  Age 3999 ms is alive, 4000 ms is stale. Closed channels stop admission and join
  the overlay worker before closing IPC; unlink/unload are idempotent.
- `sco.widgets` preserves the Titanfall panel, pilot/call/embark/unlink actions,
  match/link/frame/weapon/clip/health/range/config/peer log and refusal text.
  `sco.ui` binds F9 when available. No ImGui ABI or product menu globals are used.
  Absent widgets/hotkeys leave commands available; rendering itself is the host's
  explicit renderer adapter boundary.

## Settings and explicit unavailable features

`game`, `map`, `mode`, `extra`, `fov` retain the historical setting names as public
plugin settings. Map/mode accept only `[a-z0-9_]`. `launch` defaults true in the
manifest, but absent/failed settings never grant process launch. EA app/Origin
startup and delayed (25 s) Northstar launch happen **only after an explicit pilot
request** when no peer is linked; cancellation/unlink/unload cancels deferred
launch. No plugin-load process launch, account credentials, official-server
connection or EAC manipulation is performed.

The host's `data/titanlink.txt` root is not exposed by a public SDK contract.
Automatic legacy-file import is **unavailable**, not silently substituted by a
private filesystem helper. Copy its five keys into declared plugin settings;
keep the old file as backup. An idempotent legacy import needs a reviewed public
host data-root/migration route and remains an integration gap.

**G2 local-player relocation and G7 cvar WRITES are unapproved and explicitly
refused.** There are no queries/calls to either mutation table. Public scoped G7
bool reads do not expose `cl_fov`, so its automatic float read is explicitly
unavailable. Set vertical `fov` explicitly; zero uses the documented 60-degree
fallback and never touches a private cvar or USER.cfg. Input capture, body hiding,
Titanfall-style Star Citizen movement, camera-specific pitch, NPC hits and light
matching remain the same unported/deferred features as the bridge baseline.

## Explicit platform/renderer boundary and evidence

`platform.cpp` retains Windows-owned EA/Northstar process launch, foreground key
forwarding, client view size and validated same-user Titanfall window placement.
`overlay.cpp` retains an owned click-through/no-activate DirectComposition/D3D11
window, keyed-mutex shared texture validation/copy, and crosshair. No game device,
renderer hook or product-private symbol is used. Stop fully joins the worker before
service/channel/module lifetime ends; no timeout leaves running plugin code.

On macOS/Linux these launch/input/overlay operations explicitly report
**unavailable (requires Windows)**. Portable compile and public-wire synthetic
workflow do **not** verify Windows texture handles, GPU composition, OS shared
memory ACLs or actual paired-game behavior.

Evidence: actual independently compiled, dynamically loaded module; synthetic
public IPC adapter uses the real MIT block/ring/header primitives, synthetic
G3 rotation conversion, settings/widgets/hotkeys, malformed frames/states/ring,
heartbeat boundary, closed peer, physical partial function-pointer tails,
missing/null/failed providers and unload/reload. Targeted ASan+UBSan passes.
Checker default-folder inventory and private-dependency/symbol negative controls
pass. Fixture command/UI registration cleanup is a synthetic host stand-in, not
Windows native host fault-guard certification.

**Windows/MSVC, OS cross-process IPC, renderer/GPU, game-thread/SEH fault handling,
local peer launch/stop, in-game feature acceptance: NOT VERIFIED.** No game or
network was run. A successful checker is not host discovery or gameplay proof.
