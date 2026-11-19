# Paired-source check

A development-only check that one exact sc-offline checkout and one exact sco-core checkout belong together. You supply the commit and tree IDs of both from a reviewed record:

```
tools/transition-pair/run.sh CORE_SOURCE BUILD_DIR OFFLINE_COMMIT OFFLINE_TREE CORE_COMMIT CORE_TREE [--verify-only|--product]
```

All four IDs must be full lowercase 40-character Git SHA-1 values. Do not derive them from a moving ref, from the checkout under test, or from uncommitted work. The sc-offline IDs are supplied by you because a tracked script cannot contain the commit ID of the tree that contains it.

The runner checks the sc-offline checkout against its commit and tree and keeps its fixed ancestry anchor. It checks that the sc-offline gitlink equals the Core commit, and checks the Core checkout against its commit and tree. It prints all four IDs and the verified gitlink.

Use `--verify-only` with a clean sc-offline checkout, a clean Core checkout and a build folder that does not exist yet, outside both trees. It checks identities, cleanliness, ancestry and path containment without building or creating the folder. On Windows, run it in Git Bash with `cygpath`, and leave automatic MSYS path conversion on. The containment check compares normalized paths and the file identities of existing ancestors. It is not a defense against a path replaced while it runs.

Build mode needs a fresh folder. It clones the verified local Core objects without hardlinks, checks out the supplied Core commit, and checks its tree and cleanliness before and after CMake configuration. It does not fetch, change the gitlink or alter release workflows. After configuration it checks CMake's cache, file-API target sources and dependencies. It builds and runs all registered synthetic `tools/offline-host` tests. `--product` also configures and builds the product and needs the Windows x64 MSVC toolchain from [build.md](../../docs/build.md).

Run the identity and refusal tests with the same reviewed values:

```
python3 tools/transition-pair/test_runner.py CORE_SOURCE OFFLINE_COMMIT OFFLINE_TREE CORE_COMMIT CORE_TREE
```

They check the IDs against the immutable objects and use disposable local clones. A labelled fixture commit holds the working runner inside a temporary clone only, so the runner's clean-source check stays active; nothing from your worktree is staged or committed. The tests do not fetch. If local history is incomplete, they report the missing ancestry anchor as untestable.

This check qualifies source selection and synthetic behavior only. It does not certify a published SDK archive, native reader or worker exclusion, mappings or game behavior, and it has not been run in game ([status](../../docs/status.md)). It sets `SCO_BUILD_TESTS=OFF`, so Core's own tests are separate. `tools/check.sh` and `tools/sdk-headers.py` default to the pinned submodule, not the override, so confirm their source before you report their results.
