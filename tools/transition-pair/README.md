# Private paired-source qualification

This development-only check qualifies one exact Offline/Core source pair. Supply
both commit and tree IDs for each repository from the separate reviewed pairing
identity record:

```
tools/transition-pair/run.sh CORE_SOURCE BUILD_DIR OFFLINE_COMMIT OFFLINE_TREE CORE_COMMIT CORE_TREE [--verify-only|--product]
```

All four IDs must be full lowercase 40-character Git SHA-1 values. The runner
checks the Offline checkout against its supplied commit and tree, retains the
fixed Offline ancestry anchor, checks that the Offline gitlink equals the
supplied Core commit, and checks the Core checkout against its supplied commit
and tree. It prints all four identities and the verified gitlink. Do not derive
the expected values from a moving ref, the checkout under test, or uncommitted
work. Offline identities are caller-supplied because a tracked runner cannot
contain the commit ID of the Offline tree that contains it.

For a clean Offline checkout and clean Core checkout, with a nonexistent build
directory outside both source trees, use `--verify-only` to check identities,
source cleanliness, ancestry and path containment without building or creating
the directory. On Windows, run with Git Bash and `cygpath`; automatic MSYS path
conversion must remain enabled. The containment check compares normalized paths
and existing ancestor file identities. It is not a race-free sandbox against
concurrent path replacement.

Build mode requires a fresh directory. It clones the already verified local
Core objects without hardlinks, checks out the exact supplied Core commit, and
checks its tree and cleanliness before and after CMake configuration. It does
not fetch a remote, change the gitlink, or alter release workflows. CMake's
cache, file-API target sources and dependencies are checked after configuration.
The fixture builds and runs all registered synthetic `tools/offline-host` tests;
`--product` also configures and builds the product and requires the Windows/MSVC
x64 toolchain described in `docs/build.md`.

Run the identity and refusal suite with the same reviewed values:

```
python3 tools/transition-pair/test_runner.py CORE_SOURCE OFFLINE_COMMIT OFFLINE_TREE CORE_COMMIT CORE_TREE
```

The suite checks the supplied IDs against the selected immutable objects, then
uses disposable local clones. Its labelled test-fixture commit contains the
working runner only inside a temporary clone so the runner's clean-source gate
stays active; it does not stage or commit the caller's worktree. The suite does
not fetch when local history is incomplete and reports an unavailable ancestry
anchor as an inability to test.

This check qualifies source selection and synthetic behavior only. It does not
certify a published SDK archive, native reader/worker exclusion, mappings or
game behavior. Native activation remains refused; **not run in game**. The
fixture sets `SCO_BUILD_TESTS=OFF`, so Core's own tests are separate. Selector
compilation is not product startup wiring. `tools/check.sh` and
`tools/sdk-headers.py` default to the pinned submodule, not the override; verify
their source before reporting their results. Stable SDK archive checks remain
separate historical qualification.
