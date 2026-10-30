# G1 host-source composition

The default host builds from `external/sco-core`, pinned to development source
`e2ad885aa3027ed1162793d30292ca103c2f5b28`. It is not a published release.
For local development, an extracted `host-source-sc` kit
can be selected with `-DSCO_HOST_SOURCE_DIR=<extracted-source-root>`.
That override does not change the committed pin or the external plugin archive.
Kits must provide `sco_game_host`; a missing composition target is a configure
error when the override is used. See [candidate coordinates](sdk-candidate.md).

The game-pack composition owns original-image signature resolution, required
synchronous quit interception, app startup, plugin tick admission/cadence, task
cancellation, early DataCore/CryPak discovery and producer drain. The product
retains Windows proxy forwarding, read-only OS inspection, message/window/input/
render policy, paths/configuration, launcher consent/restoration, and remaining
legacy engine helpers. Bootstrap runs outside DLL loader lock. The WM_QUIT path
stops before doing queued work. Disabled/failed/crashed plugins have no host
feature-tick fallback.

## Development retirement boundary

The product's legacy detours, direct code writes and slot writes are not a
drainable hot-unload facility. The shell records a persistent mutation attempt
before starting the offline patch sequence, even if a later write fails. Its
mutation gate can refuse a later start, but closing it does not drain a callback,
restore a patch or establish quiescence of the startup entry gate. The image,
product code and backing state must remain valid for surviving uses; a signature
snapshot is not an image or module lease.

The product passes its conservative retirement callback to the core host. Core
invokes it on stop and failure paths, including the quit detour and admission
loss, before destructive provider teardown. The local mutation gate alone does
not make shutdown safe. If product
consumers cannot be proved quiescent, the host must close admission, retain their
backing state, report an incomplete terminal stop, refuse restart and require
process exit. Existing native hooks or byte patches can still run or affect the
game: capability revocation is not their removal. This policy does not authorize
an in-game run or assert Windows acceptance.

## Refusal boundaries

- OS module/process/service failures remain unknown, never anti-cheat absence.
  Detection is read-only; there is no anti-cheat manipulation or bypass.
- The shell collects Windows Firewall rules for the current executable and its
  TCP table through `admission_probe.cpp`. `admission_evidence.h` combines those
  readings with target-process and anti-cheat inspection. Failed or incomplete
  readings refuse admission; launcher flags and patch success do not replace them.
  Candidate adoption does not certify these collectors against a real session.
- G1 early DataCore rejects built-in plugin inputs. The shell registers none;
  features are separately built external folders. Legacy source files do not
  reintroduce registrations or fallback ticks.
- `data/plugins/quantum_drive` is an ordinary data pack. LOADED alone does not
  establish that a specific patch was applied; the consumer must inspect the
  published outcome before advertising `quantum.drive`.
- Relocation and quantum controls retain the candidate's admission, capability
  and backend checks. A successful source build does not prove movement, effects
  or restoration in the game.
- Loadout release staging, contracts providers, optional bridges and multiplayer
  remain outside this host pin update. Native services that are default OFF or
  unavailable stay so; see the candidate's release scope.

## Portable compile target

```sh
cmake -S tools/offline-host -B <outside-build-root> \
  -DSCO_HOST_SOURCE_DIR="$PWD/external/sco-core"
cmake --build <outside-build-root> --target sc_offline_host_composition
cmake --build <outside-build-root>
ctest --test-dir <outside-build-root> --output-on-failure
```

This builds the actual portable product seam and its G1 dependencies and runs
synthetic registry, retirement and hook tests, not the Windows DLL. Use the root
Windows/MSVC build for the DLL and launcher. Real game,
network, launcher-restoration and renderer acceptance require separate evidence.

## Current game-service startup limitation

The current product does not establish the durable native-reader barrier required
by the development core's game-service startup. The executable-entry gate is
released before game-service startup and can time out, so it is not that barrier.
The product therefore refuses offline host preparation before product patches or
host preparation hooks, and logs the missing barrier. Game services and dependent
offline features are unavailable with this candidate until a real startup-phase
barrier or a coordinated, proven safe core contract is implemented. The existing
retirement gate does not drain readers and cannot supply this proof.
