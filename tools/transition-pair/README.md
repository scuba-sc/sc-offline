# Private paired-source qualification

This development-only pairing checks the product's `external/sco-core` gitlink
against the selected development Core source and selects that same source through
the existing `SCO_HOST_SOURCE_DIR` mechanism. Core source is
`4e78a6a6246636d0c1f2d5f7c16e33f36b1b20dd`, tree
`b80a97a9af8d19c447399922fa0664ecc22d4190`. This local checkpoint combines the reviewed lifecycle/SDK stack with storage metadata, copied diagnostic source sections and additive widget event tags. Its unpublished `2.4.1-dev.3` archives have distinct identities recorded in [SDK coordinates](../../docs/sdk-candidate.md). The 197/200 signature scans are static only; hauling semantics and gameplay remain unverified. Earlier pairing results do not qualify this new commit. The offline branch includes
`259ee6284d4c37f2eafa32f668ff29f4bc266844`; the caller must supply the exact
paired offline commit and tree from the accompanying identity record.

For a clean offline checkout and clean Core checkout, with a nonexistent build
directory outside both source trees:

```
tools/transition-pair/run.sh CORE_SOURCE BUILD_DIR OFFLINE_COMMIT OFFLINE_TREE
```

On Windows, run this with Git Bash and `cygpath` available. The runner refuses
nonempty `MSYS_NO_PATHCONV` or `MSYS2_ARG_CONV_EXCL` because Git and CMake also
receive shell paths. It converts all paths for the selected Python interpreter
before comparing the build directory with both source roots.

`--verify-only` checks identities, source cleanliness, and build-directory
containment without building or creating the directory. It checks existing
ancestors by file identity as well as by path, so differently cased
spellings on case-insensitive filesystems cannot bypass either source root.
The check is not a race-free sandbox against concurrent path replacement.
The runner checks the CMake cache and file-API target sources and dependencies after each configure,
including the selected Core's new app source and exact-profile selector. It
requires fresh build directories, so no old library cache can be reused.
`--product` also configures and builds the product with the same Core override;
that extra step requires the Windows/MSVC x64 toolchain described in
`docs/build.md`. The ordinary path builds and runs every registered synthetic
`tools/offline-host` test, requiring key ownership, product refusal and Stop
regressions to be registered. The runner clones verified Core objects locally
into the new build directory to avoid ignored build artifacts in the input
checkout affecting compilation. The clone uses `core.autocrlf=false` so checked-out source bytes retain the immutable Git content. Offline ignored files are rejected because that
source is built in place. Both source checks reject tracked and untracked edits.
It never changes the gitlink, fetches a remote, or changes release workflows.
This Core commit is a private local candidate and is not fetchable from the
public origin unless separately published. Use its verified bundle for transfer.
The caller-supplied Offline hashes must come from the separate reviewed pairing
identity record, not from an untrusted checkout; they avoid a self-referential
commit hash in a tracked script.

Core differs from the stable pin by more than the exact-build selector. Its
C++ app Platform structure gained a default-false startup-snapshot field, so
host and Core must be rebuilt together; existing frozen C plugin ABI and
`game.build` 1.0 remain unchanged. This check does not certify a published SDK
archive, native reader/worker exclusion, 5.0 mappings or game behavior. Product
`NativeActivationAvailable()` remains hard false. Not run in game.

`tools/check.sh` and `tools/sdk-headers.py` default to the pinned submodule,
not the override; verify its source before reporting their results. Published
SDK archive checks remain separate historical qualification. Core's own tests
are separate: the offline fixture sets
`SCO_BUILD_TESTS=OFF`. Selector compilation is not product startup wiring.
