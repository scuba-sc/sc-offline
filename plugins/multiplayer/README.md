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

Keep the `multiplayer.pose`, `.spawn`, `.despawn` channels (and add `multiplayer.relay`, below). Poses are bounded at
10 Hz, name-based frames only (never remote zone IDs), strict finite/quaternion
validation and wrap-aware sequences. Match copied, observed, unambiguous local
zone names; unresolved/distant (over 10 km)/stale (10 seconds) ghosts are removed,
not placed in guessed frames. A buffer of samples with a short, capped
extrapolation moves the ghosts (see Ghost motion). No shared physics, damage,
missions or inventory is promised.

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

## Joining a relay server (sco-relay)

A session can also be hosted by a dedicated relay server, **sco-relay**, instead of by a player. The
server is peer 1 of the session, relays the players' messages to each other and spawns nothing. It
is **not a player**: it gets no ghost, is not in the "Other players" list and is not counted in
"N other players". The player still presses **Join** (address, port and passphrase of the server);
nothing connects by itself, and consent, the 12-byte passphrase minimum, the host-side admission
check and the refusal when admission is Unknown or EAC is present are exactly as for any join.

- `multiplayer.relay` is a fourth channel: reliable, from the session host only, at most 1024 bytes.
  The server sends it right after a player joins and again every few seconds (its `announce_interval_ms`,
  5 s by default): who is in the session, in which slot, and the server's player limit. The module
  reads it only from peer 1, only in version 1 and only when the length and every field are right; a
  message that is not is dropped, and `mod.log` says so once per session.
- Until the first roster arrives (up to that interval after joining) the module cannot tell a relay
  server from a player who hosts, so the join event of peer 1 makes no record for it. A host that is a
  player gets its record from its first spawn or pose, as before. From the first roster on, peer 1 is
  never added, even if it sent a pose or a spawn.
- **Nearby-only.** The roster says whether the server filters poses by distance (its `nearby_only` setting,
  off by default). When it is on, the module sends each pose to the server on `multiplayer.near_up`
  (unreliable, to the host only) instead of `multiplayer.pose`, and listens for the poses of nearby players
  on `multiplayer.near.<slot>` (unreliable, from the host only), where `<slot>` is its own seat in the latest
  roster, registered when the roster arrives and dropped when the roster says nearby-only is off, the seat
  changes or the session ends. A `near_up` message is a 40-byte header, then the pose bytes exactly as
  before: the area (FNV-1a-64 of the outermost zone's name) and the player's position in that zone, which is
  all the server reads. The server forwards the pose to the players in the same area within its
  `nearby_distance_m` (12 km by default, above the 10 km at which this module shows a player). A
  `near.<slot>` message names the sender's peer id and the generation of the slot (a counter that goes up
  each time someone new takes it); the module drops one whose generation is not the one its latest roster
  gave, one that did not come from the host, one whose sender is this player, the server or not in the
  session, and one that is not the contract (one `mod.log` line per session). A pose that arrives on
  `multiplayer.pose` is still accepted, from a session a player hosts or an unmodified player. Spawns and
  despawns are not filtered by nearby-only: they still reach everyone through the network library.
- The message layouts are in `relay_wire.h`, a **byte-identical copy** of the server's
  `server/relay_wire.h` (standard headers only, with golden bytes in the server's
  `tests/server/test_relay_wire.cpp`; the workflow test repeats the roster's). Do not edit it here:
  the contract belongs to the server, and a layout change gets a new version number there.
- Poses, spawns and despawns are unchanged: the same 10 Hz, the same messages, still relayed between
  players by the network library unless nearby-only is on (below). The module works the same on a session
  hosted by a player.

`mod.log` lines of a relay session (each starts with `[multiplayer]`; there is no passphrase, address or
player name in any of them):

| Line | When |
| --- | --- |
| `join request accepted: waiting for the session (0)` / `join request refused (N)` | after **Join**, N being the result code; accepted is not a connection |
| `relay server found: peer 1 hosts the session and relays players; it is not shown or counted as a player` | the first valid roster of a session |
| `joined relay server as peer P in slot S (generation G): N of M players in the session, nearby-only off` | the same roster, when it lists this player: **this is the line that shows a join to a relay server** |
| `relay roster: N of M players in the session, slot S (generation G), nearby-only off` | a later roster whose players, slot or mode differ (an unchanged roster is silent) |
| `relay roster: N of M players ... this player is not listed yet` | a roster that does not list this player |
| `nearby-only: poses go to the relay server on multiplayer.near_up and come back on multiplayer.near.S` | the roster says nearby-only is on and lists this player: the module now listens on its slot |
| `nearby-only is off: poses go to the other players on multiplayer.pose` | a later roster says nearby-only is off |
| `cannot listen on multiplayer.near.S (code): this player sends its poses on multiplayer.pose and will not see the players the server forwards on that channel` | sco.net refused the channel, once per session; the module tries again at every roster. Only this player's own pose falls back: the poses the server forwards to this seat have no listener |
| `ignored a malformed multiplayer.relay message from the session host; ...` (or `multiplayer.near`) | once per session, however many follow |
| `left the relay session` | the session ends or this player leaves |

`multiplayer.status` and the panel add `via relay server (not a player), slot S, nearby-only off` once a
roster has been read.

**Not run in game.** The workflow test, the relay-join test (synthetic game providers on a real
`sco::net::Core` joining a real sco-relay over loopback UDP) and the Windows build are the only evidence;
no real game, EAC, admission collector or LAN was involved. The production-shell admission blocker above
is unchanged: until a positively grounded admission collector exists, that shell refuses Host and Join.

## Ghost motion

Poses arrive about ten times a second, each later than it was sent by a different amount, and some never
arrive. The module used to draw a ghost 0.1 s behind between its last two poses; one late or lost pose
froze it at the newest, and the next one made it jump. `motion.h` (header only, no host calls) now keeps
the last 12 poses of each player:

- **Dejitter.** A pose has a time on the sender's timeline, not its arrival time: the point, at the pose's
  sequence number, of a line fitted through the arrival times of the last 16 poses. A lost pose leaves a
  gap in the sequence, which the line keeps. The line is held within 150 ms of the arrival times.
- **Playout delay.** The ghost is drawn 200 ms behind the present, so the pose after the drawn time has
  usually arrived, even after one lost pose, and the ghost moves in a straight line between the two.
- **Capped extrapolation.** If the drawn time is past the newest pose (several lost in a row) the ghost keeps its
  last velocity for at most 150 ms, then holds. A rotation is never extrapolated. The velocity is the distance
  between the last two poses over the time the **sender** took for them. The wire carries no sender time, so the
  module judges that time from the arrival times it has seen, and takes the longest of: 100 ms per sequence step
  (the sender sends one pose per tick, a tick at least 100 ms after the last; steps are counted without a cap, since
  a longer gap only lowers the speed); the timeline's own gap between the two poses; the slope of the timeline fit,
  once the fit holds 10 poses; and the newest arrival gaps when they are clearly slower than a 10 Hz sender can look
  under 35 ms of jitter, that is more than 75 ms longer than 100 ms a step (the newest pair, or the newest two or three
  intervals together; the longer spans are left out when the newest pair arrived together with its predecessor and a
  cadence is known, which is a stall let go, not a slow sender). The last is how a sender that drops under 10 Hz (a game
  under 10 frames a second) is noticed within a pose or two, before the fit has followed. The difference of two arrival
  times is never the speed by itself: poses delivered in one frame (a burst after a stall, the first two after a reset)
  arrive 1 ms apart, and a speed worked out from that would be 100 times too high. When the newest poses arrived together
  and the track is too young to know the cadence, nothing says whether the sender is a 10 Hz or a 5 Hz one, and the
  ghost holds at the newest pose instead of guessing. A second guard caps the distance: the ghost never goes on past
  the newest pose by more than 1.5 steps of movement.
- **Resets.** The buffer starts over and the ghost goes straight to the new pose, without gliding, when no
  pose arrived for 600 ms, when the pose's frames differ from the last one's (another chain of zones, a
  ship boarded or left), or when two poses are further apart than 3 km/s allows in the time the sender took (a teleport; that time is at least 100 ms per sequence step, or the track's cadence when it knows one, counted up to 50 steps: poses delivered together are not a teleport, and neither is a fast player across 10 steps at 2.5 km/s, or at 500 m a step of a 200 ms sender). A pose that is not newer than the last (wrap-aware) is dropped, as before.
- Nothing about the wire changes: the ghost's delay is the module's own business, and the server's contract
  carries no rendering parameter.

**How far the lead can be wrong.** The lead is a judgment, not a measurement, and the bounds below are what the motion
test measured on its fixed inputs, not promises. The test pins the worst lead of each case, not how many runs go over the
cap; for a constant slow sender and for the drop to 150 ms the pins equal what the version before already measured, so
they stop it getting worse, they do not prove the improvement.
"The cap" is 150 ms at the real speed (30 m at 200 m/s). "The network" is the test's own: 60 ms, +-35 ms, 12 in 100 poses
lost, 1000 runs of each case. "Before" is the `motion.h` before the slow-sender work (commit f2ba0a0).

- With exact arrival times (no jitter, no loss) the lead stays within 3% of the cap for a sender at 200 or 250 ms a pose
  (two poses delivered together, a burst after a stall, a new track), for a sender that drops from 10 Hz to 150, 200 or 250 ms
  a pose in the middle of a track (also with one pose lost), and for the four arrival patterns a review built against the
  version before (poses bunched at the start, a jitter pattern, a lost pose, a rate change). Before, these led by up to 2.5 times.
- On the network, a constant 200 ms sender is led over the cap in 243 runs of 1000, by up to 1.54 times (before: 883 runs,
  up to 1.54 times); a constant 250 ms sender in 279 runs, up to 1.39 times (before: 833, up to 1.39). The worst case did not
  improve: it sits in the first poses of a track, where two arrival times are all there is. Later in a track a
  constant slow sender goes over the cap by 1.11 times at most. The worst band is a sender at 160 to 190 ms a pose, led by
  up to 1.70 times (at 170 ms), the same as before; a game at 12 frames a second reaches 1.65 times.
- On the network, a sender that drops from 10 Hz to 150, 200 or 250 ms a pose is led over the cap in 211, 427 and 590 runs,
  by up to 1.47, 1.72 and 1.35 times (before: 660, 994 and 986 runs, up to 1.50, 2.00 and 2.50 times). A drop is noticed
  from the first slow pose when its arrival gap is clearly long (over 175 ms), and otherwise within a few poses, as the longer
  spans and the fit catch up. The large overshoot falls within 3 poses of the drop; after that the lead can stay a little
  over the cap for several more poses (at 200 ms: up to 1.13 times 4 to 10 poses after the drop, 1.09 times later).
- New 10 Hz tracks (every join, silence, change of frames and teleport starts one) stutter exactly as much as before:
  over 2000 new tracks on the network, 41 freezes and 61 jumps in the first 1.5 s, the same as before. A version that held the
  ghost at the newest pose until it knew the cadence led slow senders much closer to the cap (a constant 200 ms sender over
  it in 18 runs of 1000, by 1.05 times at most) but stuttered 191 freezes and 234 jumps on the same tracks, so it was not kept.
- Not covered: a track whose first poses arrive bunched together can mislead the cadence until it has 10 poses; a rate change
  that arrives together with jitter can still be led too far for several poses (the figures above are that case); poses of a
  sender slower than 2.5 Hz (over 400 ms a pose) are outside the range the fit reads. Two costs of the slow-sender rule,
  found by review and not fixed: a normal 10 Hz sender whose poses arrive with more jitter than the test's +-35 ms can be read
  as slow, so the ghost runs behind and catches up in jumps (one review measured 61 jumps against 39 before at +-50 ms, and
  42 against 19 at +-70 ms, over the same mature tracks; none at +-35 ms); and right after a stall the fit can read a 10 Hz
  sender as a little slower (114 ms), which raises the teleport limit, so a real jump of up to about 340 m in one step is taken
  as movement and the ghost slides and leads instead of starting over. None of this ran in game.

`tools/multiplayer_motion_test.cpp` feeds the same jittered, lossy 10 Hz path (60 ms +-35 ms, 12 in 100
lost, a burst of four lost, one pose reordered; 20 s) to the unmodified sc-offline #126 module and to this one, in real time at 60
frames a second, and counts per ghost the freezes (4 frames or more in a row moving less than a quarter of
the player) and the jumps (a frame moving more than three times the player). It prints both modules' counts, and fails unless
this module has at most half the freezes, no more jumps and a smaller largest step. It also checks the extrapolation
on its own, at walking, ship and fast-ship speeds: for two poses delivered in one frame (in steady flight, after a reset,
after a change of frames) and bursts after stalls of 150 to 250 ms, with the flow cut or resumed, no frame may lead the newest
pose by more than 150 ms at the real speed, and a burst may not look like a teleport. The same is checked for a sender
slower than 10 Hz (a pose every 200 or 250 ms, a burst of four on a steady track), for a sender that drops under 10 Hz mid-track,
for the four review patterns, for a gap of 16 sequence steps (into a new track, and on a track that knows its cadence, where the
steps must all count), and for the teleport limit at a known cadence; the network cases and the 2000 new 10 Hz tracks are run
as described above. `multiplayer-motion-test --track-tests` runs only these buffer tests, without a module.

## Tester cases (all **not run in game**)

The production shell refuses Host and Join until an admission collector exists (above), so every case
that needs a connection needs a shell that grants admission. Report what `mod.log` says.

1. Start the game with the module installed and do nothing: no `[multiplayer]` line, no socket, **Join**
   has not been pressed.
2. In the production shell (admission Unknown) press **Join** with a sco-relay server's address and
   passphrase: the panel says `Join request refused (1)` and `mod.log` has `join request refused (1)`;
   nothing is sent. Same with EAC running.
3. In a shell that grants admission: **Join**. Expect `join request accepted: waiting for the session (0)`,
   then within the server's announce interval `relay server found: ...` and `joined relay server as peer P in slot S (generation G): ...`.
4. Two players on the server: each sees one ghost, the panel lists one other player, and the server is not in the list.
5. Press **Leave**, or stop the server: `left the relay session`, the ghosts go away.
6. A session a player hosts: no relay line in `mod.log`; everything as before.
7. A third player in another place with nearby-only on: no ghost for them, `nearby-only: poses go to the relay server ...` in `mod.log`.
8. The other player walks, stops and turns: the ghost follows about a quarter of a second behind with no freeze
   at a dropped message and no jump; a quantum jump or a respawn moves the ghost at once.

## Build/install and evidence boundary

The pinned published **sdk-v2.2.0 contains G13, G3 and retained widgets**
(`sc_private_session.h`, `sc_game_spatial.h`, `sco_widgets.h`); sdk-v2.0.0 did
not. The CI release job does not yet stage multiplayer. The external target
refuses a missing header and never adds a host include root. An isolated local
compile uses the checksum-verified published `sco-sdk-2.2.0.zip`:

```
cmake -S plugins/multiplayer -B build-multiplayer -G Ninja \
  -DSCO_SDK_INCLUDE_DIR=/path/to/extracted/sco-sdk-2.2.0/include \
  -DSCO_WORKFLOW_TESTS=ON
cmake --build build-multiplayer --target multiplayer-module-test
ctest --test-dir build-multiplayer -R '^multiplayer-module-workflow$' -V
cmake --install build-multiplayer --component multiplayer --prefix stage
```

`-DSCO_WORKFLOW_TESTS=ON` also builds `multiplayer-motion-test` (ctest name `multiplayer-motion`, about 25 s in
real time). Add `-DSCO_MOTION_BASELINE_DIR=<folder with the unmodified multiplayer.cpp and multiplayer_wire.h of
sc-offline #126, commit 481b99e>` and it runs that module beside this one on the same path and compares them.

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
