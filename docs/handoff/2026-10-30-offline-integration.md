# sc-offline handoff — 2026-10-30 (draft)

Update 2026-10-31: the local development gitlink now points to unpublished core
#304 `1166c9f12d7327dd50a58b1d9261e7d6cc21bef6`; the external plugin archive
remains RC2. The older pin and Windows evidence below describe the original
handoff, not this updated source. See `2026-10-31-native-reader-barrier.md` for
the separate registry activation blocker.

ASOP startup: on core `1166c9f`, failed catalogue installation can leave
admission open while later installers run. The product now calls Stop if Start
returns `Unavailable` or throws a C++ exception, preserving the failure and
retaining hook backing. This is a post-return backstop, not a replacement for the
core fix that closes admission within the failed installation path. Do not claim
that this candidate has the core fix until the coordinated gitlink is updated.

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
- The separate owned-registry consumer refuses game services without a real native-reader barrier. That consumer is not present on this branch; preserve its refusal when coordinating the integration.
- The original handoff embedded pin was `844e136`; the local development pin is now the #304 SHA above, not a published SDK. External plugin archive remains `sdk-v2.4.0-rc.2`.
- Draft #117 (ASOP ship selector) remains parked; native `Reconfigure` refused in core.
