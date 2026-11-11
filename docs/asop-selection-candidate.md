# ASOP selection file candidate

This source-only candidate connects the public spawn-plugin selection panel to a host-owned file coordinator. It does not connect a native terminal hook, switch ASOP mode, publish a selection to the game, change the SDK pin, or establish gameplay evidence.

`offline::asop_selection::FixedFile` resolves `asop_ships.txt` using the existing `DataFilePath` sibling policy. The configured path must be absolute and is copied at initialization. Its `Adapter()` returns the host-only callbacks used by `sco::game::asop_selection::Start`; the `FixedFile` object must remain alive until after `Stop`. The coordinator invokes `Pump` on the game thread to process at most one accepted file edit. The development-only `SCO_ASOP_SELECTION_COMPOSITION` option is currently rejected by CMake: `selection::Stop` requires the game thread, and the product has no nonblocking close/drain ordering that keeps its provider alive until stop completes. The option remains OFF by default. The file reader itself is unchanged: it reads the configured full `ships.txt` twice and refuses a missing, changing, oversized or invalid catalogue instead of publishing a partial list; a refused startup requires a host restart to retry.

The Windows implementation takes a per-path advisory mutex for cooperating instances, reads stable complete bounded documents, compares the copied base and file identity, writes and flushes unique temporary and candidate files, and records a flushed recovery marker. It asks the coordinator's permit immediately before `ReplaceFileW` for an existing target or fail-if-exists `MoveFileExW` for an absent target. It checks the displaced backup and reads back the canonical target. A competing noncooperating editor can still race the OS replacement. Unexpected or uncertain outcomes retain the marker and artifact stem in `Diagnostic()` and never report a successful commit. Startup/refresh clears a marker only for a verified unstarted preparation or a verified completed replacement; ambiguous states remain unavailable for manual reconciliation. The files are not a cross-process compare-and-swap or file-plus-engine transaction, and crash/power-loss guarantees depend on Windows and the filesystem.

When the optional consumer path is composed, a committed file notice remains pending until host `Reconfigure` returns `Ok`; a parse failure, exception or refusal is logged and retried, and an older apply cannot clear a newer notice. Current Core main intentionally returns `Unavailable` from `Reconfigure`, while the product native-activation gate refuses before the adapter starts. The retry logic is synthetic scaffolding only: no live state changes, engine quiescence or game acceptance are established.

The proposed startup file reader checks for a pending marker even when the selection file is absent. It does not acquire the adapter's document lock: checking for a marker and reading twice cannot serialize the startup snapshot against a concurrent replacement. A coordinated read/write protocol is required before this composition can be enabled.

`tools/asop_selection_file_test.cpp` is a Windows temporary-directory test. A MinGW x64 build passed on Windows AMD64 both with production code and with `SCO_ASOP_SELECTION_TESTING` fault hooks. The latter exercises controlled edits before and after replacement, checks that conflicting versions remain recoverable, and terminates child processes before and after replacement to exercise recovery on the next read. It is not a complete I/O-failure, power-loss, filesystem, race or lifecycle matrix. The actual spawn module was also tested with Core's widgets, selection coordinator and release path using a synthetic document; the combined synthetic host-lifecycle test exercises the source composition using an in-memory document. Native terminal behavior and production acceptance remain unverified. The development option is not a release or game-acceptance claim.

The retained host menu client is separately gated by `SCO_HOST_MENU_CLIENT`, OFF by default. Enabling it requires the host-menu header and widgets 1.3 renderer; configuration refuses missing prerequisites. The selection and menu options are independent and do not enable native publication.

When the pinned host lacks the development host headers, its syntax check
preprocesses the two optional implementation files out. Selecting either ASOP
composition now refuses configuration before header checks because no reviewed
product stop/drain order exists. CI exercises the spawn selection workflow,
environment policy, notice retry model and refusal gates using synthetic
fixtures. The Windows pinned-host test job also builds and runs the standalone
file adapter test in production and fault-hook variants. It does not compile the
combined Offline host composition fixture. Those checks do not establish native terminal
behavior. This preflight does not publish an SDK package or enable product
startup.
