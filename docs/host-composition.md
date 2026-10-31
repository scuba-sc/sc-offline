# G1 host-source composition

**Development startup is refused:** the product does not install the entry hook,
call native host Prepare/Start, apply product patches, or activate plugins until
native reader/worker exclusion and drain have been established. The composition
description below describes the intended lifecycle, not a currently running host.

The default development host builds from `external/sco-core`, pinned to
unpublished sco-core #305 source `49eb153d1d0952ebbc7baa258988e6dc6cc67320`.
For local development, an extracted `host-source-sc` kit
can be selected with `-DSCO_HOST_SOURCE_DIR=<extracted-source-root>`.
That override does not change the committed pin or the external plugin archive.
Kits must provide `sco_game_host`; a missing composition target is a configure
error when the override is used. See [candidate coordinates](sdk-candidate.md).

The #305 host separates registry preparation from activation. Its strict owned
host keeps the candidate registry private; `AcquireHostSignatureSnapshot` reads
the separately installed convenience registry and is not an accessor for that
candidate. The strict activation currently rejects the required native hooks,
game services and legacy product adapters. The product cannot supply the actual
image/resolver ownership or native reader/worker exclusion and drain needed to
change that. A copied snapshot does not lease image memory or exclude readers.

The old G1 path's quit interception, early DataCore and plugin startup are
therefore not active. The product still contains Windows proxy forwarding,
read-only OS inspection, message/window/input/render policy, paths/configuration,
launcher consent/restoration and legacy engine helpers. A failed or uncertain
Stop latches restart refusal; the process, module and backing must remain live
until exit. The latch does not itself own or drain them. Core retains its owned
closure on its terminal paths, but legacy consumer retention is not established.
Core's terminal startup or stop failure also requires process exit. No callback
is treated as drain proof.

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
```

This builds the actual portable product seam and its G1 dependencies, not the
Windows DLL. Use the root Windows/MSVC build for the DLL and launcher. Real game,
network, launcher-restoration and renderer acceptance require separate evidence.
