SC Offline Desktop 0.1 installs a dedicated Windows launcher with desktop/Start
menu shortcuts and a Windows Apps uninstall entry. No PowerShell or batch file
needs to be run when playing.

### Downloads

- **SCOfflineSetup-0.1-win-x64.exe**: the recommended Windows installer.
- **SCOffline-0.1-win-x64.zip**: the complete portable launcher and runtime; extract
  the entire archive and keep its files together.
- **SHA256SUMS.txt**: SHA-256 checksums for both downloads.

Requires Windows 10/11 x64 and your own installed copy of Star Citizen. The
installer includes its offline runtime and downloads nothing during installation.
It contains no Star Citizen game binaries or account files.

### Behavior

- The launcher starts an offline session and stays open to supervise cleanup.
- Session firewall rules cover the game, RSI Launcher, the native runtime and
  crash reporter; unrelated applications retain Internet access.
- On game exit, native mod changes are restored before session rules are removed.
  Restore online handles interrupted sessions; incomplete cleanup retains protection.
- Session crash reporters are closed automatically, without terminating another
  application's same-named report process.
- Play has **no arbitrary minimum of 32 GB of free RAM**. Memory information and
  recorded user/profile errors are reported without changing account credentials.
- Uninstall preserves player saves/settings. Runtime updates do not silently follow
  development main.

### Runtime and validation

Desktop 0.1 wraps the unmodified published **sc-offline 0.7.7** runtime, pinned by
source revision and SHA-256. `runtime-lock.json` and the included licence notices
describe it. This does not rebuild, qualify, or bypass the current development
host's startup gate. Desktop tags are separate from native runtime versions.

The native helper temporarily changes the hosts file and renames Easy Anti-Cheat's
executable during an offline session, restoring its recorded changes afterward.
This release must not be described as never touching EAC. Do not run official
online play concurrently with an offline session.

Windows x64 compilation and 32 local checks passed. Install, upgrade,
uninstall/reinstall save preservation, real IPv4 firewall isolation and fixture
crash-reporter cleanup were tested. A user reported working offline gameplay on
game build 4.10.201.48085; performance, IPv6 traffic, power-loss recovery in a live
game, and all gameplay features are not qualified. Hosted CI was unavailable for
this release; the binaries were built and checked locally.

Using a third-party mod may violate RSI's terms and carry account risk. Network
isolation is not a guarantee against account action. This project is not endorsed
by CIG or RSI.
