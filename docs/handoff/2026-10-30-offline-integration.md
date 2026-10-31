# sc-offline handoff — 2026-10-30 (draft)

Current local work refuses native host Prepare/Start and the entry hook until a real reader/worker exclusion and drain protocol exists. The product is not playable. The development gitlink and generated CI preflight now use core #304 `571fe8e2c911d7c63c372103724896feaabe58f2`; this is not a published SDK release.

Earlier 2026-10-31 integration used unpublished core
#304 `1166c9f12d7327dd50a58b1d9261e7d6cc21bef6`; the external plugin archive
remains RC2. That earlier pin and the Windows evidence below describe earlier source, not the current candidate. See `2026-10-31-native-reader-barrier.md` for
the separate registry activation blocker.

ASOP startup: on core `1166c9f`, failed catalogue installation can leave
admission open while later installers run. The product now calls Stop if Start
returns `Unavailable` or throws a C++ exception, preserving the failure and
retaining hook backing. This is a post-return backstop. Core `571fe8e` also refuses ASOP Start before admission or installation until a real safe phase exists.
Opting into `SCO_ASOP_HOST_COMPOSITION` excludes legacy fleet/services; a
refused replacement does not reactivate them. Neither that option nor the core
pin restores ASOP or product gameplay.

Branch `handoff/offline-integration-20261030` = main `0f569b9` + 4 commits:

| Commit | Change |
|---|---|
| `e0183cc` | fleet: retain stable issued identities; snapshot before locking |
| `f8ce2ef` | fleet identity regressions + package inventory gate |
| `29ebb16` | VPN: admit explicitly configured firewall gaps (#183 line) |
| `6ce48b3` | VPN: validate canonical environment ranges; clarify policy |

Canonical cross-repo runbook: sco-core branch `handoff/core-integration-20261030`,
`docs/handoff/2026-10-30-cloud-handoff.md`.

## Verified
- macOS: fleet identity sanitizer fixture (7 scenarios); admission ASan/UBSan 61 cases / 323 checks; manifest 17/17, NPC 4/4, external release 3/3, plugin profile 10/10, host menu, diff whitespace.
- Windows (MSVC 19.44, /W4 /WX) on exactly `6ce48b3`: full Release product build passed with published host pin `844e136`; admission 61/323; launcher/parser to Win32 env to child admission 18 checks; identity fixture 7/7.
- Same Windows build also passed against an extracted core candidate via `SCO_HOST_SOURCE_DIR` (development override only; pins unchanged).

## Not verified / limits
- No game run, no live firewall, no native fleet/hook/SEH acceptance.
- Identity fixture uses synthetic SRW/SEH/registry seams.
- The original handoff accepted public IPv4 exceptions; the local correction
  rejects them. Environment values still do not prove launcher provenance.
- The separate owned-registry consumer is not present on this branch. The current local product preflight independently refuses native activation until a real native-reader barrier is available.
- The original handoff embedded pin was `844e136`; the local development pin is now the #304 SHA above, not a published SDK. External plugin archive remains `sdk-v2.4.0-rc.2`.
- Draft #117 (ASOP ship selector) remains parked; native `Reconfigure` refused in core.
