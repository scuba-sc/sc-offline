# sc-offline handoff — 2026-10-30 (draft)

**Registry migration draft:** based on offline `772129e09e7838d56165bd3613f43ac49d373bb8`, with local development core #305 `a5101d406a9ddbff14e437408686ffb04266be18`. The historical #304 coordinates below are not this draft’s gitlink. Native startup remains refused; no published SDK or gameplay claim follows.

The merged #304 integration refused native host Prepare/Start and the entry hook. Its gitlink and generated CI preflight used core #304 `1b6782bc5c8b7d189cdff4c83e81957f09af35d7`; this was not a published SDK release.

Earlier 2026-10-31 integration used unpublished core
#304 `1166c9f12d7327dd50a58b1d9261e7d6cc21bef6`; the external plugin archive
remains RC2. That earlier pin and the Windows evidence below describe earlier source, not the current candidate. See `2026-10-31-native-reader-barrier.md` for
the separate registry activation blocker.

ASOP startup: on core `1166c9f`, failed catalogue installation can leave
admission open while later installers run. The product now calls Stop if Start
returns `Unavailable` or throws a C++ exception, preserving the failure and
retaining hook backing. This is a post-return backstop. Core `1b6782b` also refuses ASOP Start before admission or installation until a real safe phase exists.
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
- The old separate owned-registry consumer branch was not included in the #304 integration. This draft migrates its own consumer against #305, and the product preflight still refuses native activation until a real native-reader barrier is available.
- The original handoff embedded pin was `844e136`; the merged #304 integration used the SHA above. The current migration draft uses #305 as noted at the top. The external plugin archive remains `sdk-v2.4.0-rc.2`.
- Draft #117 (ASOP ship selector) remains parked; native `Reconfigure` refused in core.
