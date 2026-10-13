# External multiplayer (G13)

`multiplayer` is an ordinary enabled native plugin, never a built-in. Install its
independent component into `data/plugins/multiplayer`; `plugins = on` and a newer
G13 host/SDK are required. Removing/disabling the folder never activates a host
fallback. Host source retains the inactive old implementation for later cleanup.

## Preserved surface and secure defaults

- `multiplayer.status`, `multiplayer.leave`, `multiplayer.goto <player>`.
  Goto always reports **G2 mutation not approved**, even if another table offers
  movement. There is no private relocation fallback.
- Retained **Multiplayer** panel: Host, Join, Leave, player name, numeric IPv4
  address, host/join ports, local avatar archetype, show ships, remembering and
  authenticated peer list. Host-side `sco.widgets::ImGuiRenderer` draws it on the
  existing game-thread frame handoff; no ImGui context/types enter the module.
- Host/Join are deliberately UI-only operations. Loading, changing settings,
  ticks, peer packets and automatic retries never set consent or open a socket.
  Requests borrow copied host-bound credentials and validate complete callbacks.
  Acceptance is not proof that a peer has authenticated/connected.
- The host-owned G13 broker is ON by default, but **loopback-only with no startup
  socket**. Admission requires positively certified offline session, no EAC,
  blocked official network and the right process. Unknown/online/EAC refuses;
  admission loss and owner unload close the session. Broker authenticates joins,
  bounds peers/messages/rates and audit-logs its lifecycle without credentials.
- Passphrase minimum is 12 bytes. The panel masks input with the additive
  `SCO_WIDGET_PASSWORD` flag; remembering defaults false. After Host/Join an
  unremembered plugin/UI credential is cleared. Masking is display protection,
  not encrypted memory or storage. The public widget service copies text and
  native plugins run with host rights. Remembering explicitly stores plaintext
  in the existing optional DB key and displays a warning; disable it to delete
  the key transactionally. User DBs are never copied into release resources.

## Settings compatibility

Owner remains **`multiplayer`**, database **`data/storage/multiplayer.db`**. Reuse
`name`, `join_address`, `avatar_class`, `host_port`, `join_port`, `show_ships`,
`remember_passphrase`, optionally `passphrase`. Integer values remain decimal
text; all writes are in one public `sco.storage` transaction. Do not rename,
truncate or erase existing databases. With no storage, settings remain in memory
and the panel reports save failure. There is no import of host files or secrets.

## Explicit LAN host policy

The module cannot choose a bind address, widen scope, run a socket, select a
transport or bypass admission. A host-source product sets
`Platform.privateSession.bindAddress` and `.allowedAddresses` explicitly.
The sc-offline shell also accepts host-selected environment policy:

```
SC_OFFLINE_MULTIPLAYER_BIND=192.168.50.10
SC_OFFLINE_MULTIPLAYER_PEERS=192.168.50.11,192.168.50.12
```

These are synthetic examples, not observed user addresses. Both values are
required for LAN; the broker accepts only a numeric private local interface and
1–16 exact private peer addresses. DNS, public/wildcard/CIDR/IPv6, duplicates,
missing/truncated inputs and implicit LAN expansion refuse. Use the appropriate
local interface and reciprocal peer policy on each instance. Default/unset is
127.0.0.1 only. `SC_OFFLINE_MULTIPLAYER=off` disables the host broker while the
plugin can still expose status. Legacy `multiplayer_allow` CIDR ranges do **not**
authorize G13 and are never imported into broker policy. Existing launcher
firewall allowances are not broker admission or a guarantee of reachability.
Actual interface/firewall behavior still requires owner Windows acceptance.

**Current production-shell blocker:** its G1 inspector still reports live
session/effective official-network evidence Unknown; it does not certify these
from launcher flags or offline patch success. G13 therefore refuses in that
shell until a positively grounded admission collector is provided. No bypass or
invented evidence was added to make this feature appear live.

## Co-presence, not shared simulation

Keep the `multiplayer.pose`, `.spawn`, `.despawn` channels. Poses are bounded at
10 Hz, name-based frames only (never remote zone IDs), strict finite/quaternion
validation and wrap-aware sequences. Match copied, observed, unambiguous local
zone names; unresolved/distant (over 10 km)/stale (10 seconds) ghosts are removed,
not placed in guessed frames. Two samples provide buffered position/rotation
interpolation without extrapolation. No shared physics, damage, missions or
inventory is promised.

Avatar ghosts use the locally selected NPC archetype; peer avatar classes are
informational rather than arbitrary remote actor-spawn authority. Ship ghosts
are bounded, class-syntax-checked and validated by the owning entity provider.
Only authenticated session members allocate peer records; at most 15 peers and
32 active/pending-removal ghosts. Actors use `game.actors.spawn_npc/despawn`,
ships use `game.entities.spawn/despawn`, and transforms are owned public calls.
Streaming/retry/removal tickets are bounded. An ambiguous `alive=0` never erases
ownership; provider unload cleanup retains any refused removal. There are no
private NPC-remover, spawner, vehicle-registration or zone-pointer fallbacks.

The legacy v1 despawn helper wrote 16 bytes but declared/validated 12, making all
messages fail. The external v1 consumer declares and checks the actual 16-byte
frame; no active built-in peer format is silently claimed compatible.

## Build/install and evidence boundary

Published **sdk-v2.0.0 does not contain G13, G3 or retained widgets**. Its CI pin
is intentionally unchanged; automatic CI release-job promotion must wait for an
approved matching SDK. The external target refuses a missing header and never
adds a host include root. An isolated local compile can use a checksum-verified
archive generated from the G13 core branch (not a published release):

```
cmake -S plugins/multiplayer -B build-multiplayer -G Ninja \
  -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/sco-sdk-2.0.0/include \
  -DSCO_WORKFLOW_TESTS=ON
cmake --build build-multiplayer --target multiplayer-module-test
ctest --test-dir build-multiplayer -R '^multiplayer-module-workflow$' -V
cmake --install build-multiplayer --component multiplayer --prefix stage
```

The enabled `plugin.ini` and module install into `stage/data/plugins/multiplayer`
without a `disabled` marker. Windows emits `multiplayer.dll`; macOS produces a
native development `.so` for the targeted dynamic-loading fixture, not a player
Windows package. Shared plugin builds include this target by default when the
extracted SDK contains `sc_private_session.h`, and explicitly report unavailable
otherwise; no published CI job pretends to ship it without the contract.

The fixture loads the actual independent module with synthetic public providers,
checks no auto-host/join, Host/Join consent and credential bounds/masking/defaults,
exact storage keys, refusal, owned ghost messages/transform/despawn, leave and
reload. Core separately exercises the real authenticated backend/broker through
a synthetic transport. Neither proves real Windows/GPU/render/input, admission
collection, LAN reachability, native unload/fault cleanup or gameplay. Those are
**NOT VERIFIED**; no full suite or actual game/network session was run here.
