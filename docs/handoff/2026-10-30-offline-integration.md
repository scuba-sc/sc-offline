# sc-offline handoff — 2026-10-30 (draft)

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
- Explicit VPN ranges may include public IPv4; environment values do not prove launcher provenance.
- Startup blocker unchanged: `offline_host::Prepare` still refuses game services without a real native-reader barrier. Keep that refusal.
- Published SDK pin stays `sdk-v2.4.0-rc.2` / `844e136`. Do not repin until the owner publishes 2.4.0.
- Draft #117 (ASOP ship selector) remains parked; native `Reconfigure` refused in core.
