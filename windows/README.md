# Windows desktop installation

This optional Windows frontend installs **SC Offline** as a per-user application,
with its own launcher, desktop and Start menu shortcuts, and a Windows Apps
uninstall entry. Playing and cleanup do not run PowerShell or batch scripts.
Windows 10/11's .NET Framework 4.x is used; no additional package manager, SDK or
runtime download is required on the player's PC.

## Player flow

Run `SCOfflineSetup.exe`, install, then open **SC Offline**. Select the installed
game channel folder containing `Data.p4k` and `Bin64`. Press **Play offline** and
approve Windows' administrator prompts for session protection and the native
runtime helper. The game itself starts with the native launcher's normal user
token. Leave the launcher open. Closing the game automatically restores the mod,
temporary hosts/EAC changes and session firewall rules. Closing the launcher during
a session offers to stop the game and exits after cleanup. A separate elevated
guard survives an abruptly terminated UI process.

The frontend blocks every address and protocol for the selected game's executables,
RSI Launcher, crash reporter, EAC and packaged native runtime for that session.
It closes RSI Launcher after protection is established. It does not disconnect
network adapters or block unrelated applications. Multiplayer, Discord presence,
automatic updates and automatic crash-report browser links are disabled for this
strict offline profile. Windows Firewall must be enabled for all profiles and
permit local policy changes; otherwise Play refuses to start.

After a crash or restart, **Restore online** completes native cleanup before
removing recorded frontend rules. A cleanup failure retains protection and reports
the failure. Uninstall refuses while a game, launcher or recovery journal remains.
Saves and settings are preserved outside the app directory when uninstalling.

Isolation limits network exposure; it is **not a guarantee against account action**.
No account credentials, entitlement changes, online services or game files are
part of this frontend or installer.

## Memory and the observed crash

Stanton (`boot_map = PU`) is the default. Loading every system (`PU_All`) is optional.
Memory is informational only. There is no free-RAM or commitment threshold that
blocks Play, and no requirement to have 32 GiB free. Memory policy is not a
compatibility fix for the legacy runtime.

Local 4.10.201.48085 runs with legacy runtime 0.7.7 and both `PU_All` and `PU` ended with
`STATUS_CRYENGINE_OUT_OF_SYSMEM`: about 25 GiB game commitment, a 52 GiB system commit
limit, and only 5 MiB remaining for the game. Free disk space and installed RAM are
different from available commitment. The UI classifies this specific failure from
the game log rather than attributing every crash to memory. User/profile errors
are reported separately when actually present in the matching session log. Stanton-only startup
did not resolve that observed limit; additional commitment is still required.

**Virtual memory** optionally configures a 32–48 GiB paging file on the system drive,
preserves other configured paging drives, saves the previous configuration locally,
and requires a Windows restart. It never restarts Windows automatically. This is a
persistent, explicitly selected Windows setting, separate from temporary session
changes. At least 36 GiB of free system-drive space is required before changing it.

## Runtime provenance and upstream boundaries

`runtime-lock.json` pins the published legacy 0.7.7 ZIP, source commit and SHA-256.
The build verifies the ZIP and every original manifest entry before packaging it.
The installed frontend checks executable/DLL hashes before each session. The native
CLI still owns DLL installation, launch and cleanup; no game hooks or SDK adapters
are duplicated here. An older EAC backup is preserved across a session.

**This is not a build or qualification of the current development host.** Its native
startup refusal stays untouched. Packaging a legacy release does not enable current
SDK capabilities or establish game compatibility. A runtime-pin update is a separate
review: update the lock from an actual published release, verify provenance, rerun
the tests, and qualify startup and cleanup on the exact game build. Never replace
the locked runtime with moving `main` or remove host admission gates.

No game binaries, private logs, paths, build outputs or downloaded runtime DLLs are
committed. `windows/` is additive and does not modify `src/`, `launcher/`, `plugins/`,
SDK pins or native build semantics, keeping upstream synchronization separate.

## Build and verify

On Windows, using the C# compiler included with .NET Framework:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/build.ps1 -RunTests
```

The only build-time download is the checksum-pinned public release archive. An
offline build can supply `-RuntimeArchive <verified-zip>`. Output:
`windows/out/SCOfflineSetup.exe`. The installer embeds the full package and does
not download anything when installing. Build scripts are not installed or launched
by the player applications.

Unit checks cover release policy on cleanup failures, user-error classification, INI
normalization/injection, OOM diagnosis, atomic journals and archive traversal.
An elevated `SCOfflineLauncher.exe --test-firewall` performs a real TCP reference →
blocked → restored test against `api.github.com:443` using a child copy of the same
executable identity. It does not contact CIG or launch the game. IPv4 connectivity
is tested; rules cover both address families. Test evidence stays in local app data.
Run this only as an explicit validation action; it is never part of ordinary play.

### Recorded local validation

- Built x64 with the Windows .NET Framework compiler, treating warnings as errors.
- 32 checks passed, including real fixture processes verifying that cleanup stops
  only the selected game's crash reporter and leaves an unrelated same-named process alone.
- Installed and registered the app; upgraded, uninstalled and reinstalled it.
  A save fixture survived both upgrade and uninstall/reinstall, and the Windows Apps
  entry was removed and recreated correctly.
- The real IPv4 firewall reference/blocked/restored test passed. IPv6 traffic was
  not exercised; the rules use all addresses and all protocols.
- On game **4.10.201.48085 (CL 13220821)**, the legacy runtime started with
  `bOnline[0]`, but both all-system and Stanton-only runs exhausted system commitment
  before usable gameplay. Native cleanup completed and frontend session rules were removed.
- The initial frontend refused another launch with roughly 24 GiB available;
  that arbitrary minimum was subsequently removed. Current Play has no memory gate.
  A 32–48 GiB paging-file configuration was applied locally but requires a restart;
  **successful gameplay after that restart has not been verified**. Do not describe
  the memory policy or installer as proof that the legacy runtime is playable on this build.
- Abrupt UI-process termination and power-loss recovery are implemented and covered
  by cleanup-policy fault checks, but were not exercised with a live game.

Local state and logs: `%LOCALAPPDATA%\sc-offline-desktop`. Native runtime logs:
`%LOCALAPPDATA%\Programs\sc-offline-desktop\runtime\data`. Review and redact them
before sharing. In-game acceptance and CI are separate from these Windows tests.
