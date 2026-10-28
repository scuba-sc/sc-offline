# SDK/MCP RC2 validation receipt

Recorded 2026-10-28 against sc-offline base `7335c9bbbc36f6d82c6cc3958cccbfada9b8f222` and [published SDK RC2](https://github.com/scuba-sc/sco-core/releases/tag/sdk-v2.4.0-rc.2). The release metadata identifies source `844e136ccb280ad8cb43f73bc43c9447e8f6799d` and builder run `40655327650`, attempt 1. The release is marked `prerelease=false`; this is not gameplay or final acceptance evidence. The product host and plugin archive are distinct inputs.

| Input | SHA-256 |
| --- | --- |
| Release `SHA256SUMS` | `cf118d4132a4b577fe16940e355fae51981bcad533d5252ca1b9850a31a9281d` |
| Combined `sco-sdk-2.4.0.zip` | `51a7ce266d087bcbeac39c92f26a6f905b4473883dd7ed6f0f5ca823fa88c88e` |
| Core `sco-sdk-core-2.4.0.zip` | `052189fde7d3f1f7720e634a1480e6a8b86638b8f61607b45025ac317d0186ea` |
| Overlay `sco-sdk-game-sc-0.8.0.zip` | `7c03a9aef7c5e3e7ab201d6a7fd6aef024047cc20dba32b950789482d57122b0` |
| Combined catalogue | `b36b5be29f54d50389dd352b0c0222643e2566f936bfecc36fea665641f8aea2` |
| Combined workflow index | `df281ece5116e7f86df093352a370e843563330d306698903d60848928962f56` |
| Packaged `docs/roadmap.md` | `a37877ecfe1aca9bd6d980a0d642bdce775414e9b0cc5c04fce28a9bfa32b333` |

`sha256sum -c --ignore-missing SHA256SUMS` passed for the three downloaded archives. The combined tutorial proof verified the full extracted inventory. The overlay's descriptor names the exact core archive digest and the same source; composing both verified every entry in both internal checksum inventories and required every overlapping file to be byte-identical. The MCP script below targets the combined profile, not the composed profile.

On Debian Linux x86-64, Python 3.12, GCC 14.2 and CMake 3.31.10, `python3 examples/sdk-mcp/check_rc2_mcp.py --sdk <verified-combined-root> --transcript <new-file> --scaffold <new-directory>` passed. The actual server negotiated protocol `2025-11-25`, advertised 14 read-only discovery tools and six with `--minimal-tools`, returned all six workflow entrypoints and every indexed reference byte-for-byte, and reported bundle 0.2.2, baseline SDK 2.4.0 and the source above. Guide search and section retrieval succeeded from the matching guide index. Exact version comparisons matched 2.4.0/0.8.0 and rejected a 9.9.9 match. The optional historical checkout approval-record lookup intentionally accepts stable tag syntax only and refused the RC2 suffix as advertised. It does not inspect the current release asset, and its refusal is not a failure of RC2 catalogue, guide or workflow discovery. Compare SDK/game-pack/source coordinates and verify the release archive separately. The server refused a stale source index, unknown workflow, traversal, tools/list before initialization, and a Lua scaffold requesting `game.inventory`; it reported the layout capability default OFF, synthetic-only evidence and unknown gameplay receipt. These are static metadata observations, not a runtime capability probe. The full local request/reply transcript was retained outside the checkout.

The bounded planning goal was an author-owned neutral C command and optional widget panel, with a missing-storage fallback. `sco_widgets_v1.set_panel` is a C direct mapping with a 16-byte member extent; the queried contract describes ownership, callbacks, game-thread rules and removal. MCP generated `rc2_probe` (ABI 1.1), then `cmake -S <scaffold> -B <build> -DSCO_SDK=<verified-combined-root> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and `cmake --build <build> --target check` passed. Full and short-root synthetic checks both reported `pong (0 of 1 services)` and `OK: 0 failure(s)`; the full checker separately reported the absent storage service and continued. The short-root check does not prove old-host admission. The generated module was ELF x86-64 despite its `.dll` name, so it is a Linux development artifact.

For the packaging workflow a local development ZIP contained only `data/plugins/rc2_probe/{plugin.ini,rc2_probe.dll,LICENSE}`; fresh extraction matched the staged bytes and the extracted folder passed the same full and short-root checker with the exact ping reply. The local ZIP SHA-256 was `625d2879879624bc0f5dc6506e41fdd355eb1440e104cb3878c9107cf01a528f`. It is not a Windows distribution. The six workflow entrypoints and all references were retrieved and checked; this bounded exercise covers planning, building, debugging the missing-provider fallback, and packaging. Upgrade guidance was evaluated for the selected current kit without a historical migration, and gap-report guidance was read for the documented Lua limitation; a separate real-world upgrade or submission was not performed.

The copied tutorial initially failed because it omitted `plugins/picker.h` and NPC `.inc` files; the copy script now includes both. With those fixes, the full `tools/check_sdk_tutorial.py` command in the [example README](README.md) passed against the published combined RC2 archive. The NPC named dynamic fixture passed **8 cases**; the inspector workflow and named module fixtures passed **15** and **14 cases**. Both proofs verified the archive inventory, traced two compiler translation units, checked their staged plugins and rejected the private host header, product header, private host symbol and system-marked transitive foreign header mutation controls. A separate compiler-and-tracer check also rejected source and extracted-SDK symlinks to an untrusted foreign header.

The initial run on this Debian image stopped at its system `asm/errno.h`: GCC reports the resolved `/usr/lib/linux/uapi/x86/asm/errno.h`, outside its normal include search roots. No tracer rule was relaxed. The passing run used Debian `linux-libc-dev` version `6.12.107-1`, architecture `all`, obtained with APT from signed Debian metadata and extracted locally. The `.deb` SHA-256 was `dc68dd53c7356598d005866da0d19d7bd33af2a62971aced624cdf04492902b4`. Its `asm/errno.h` matched the installed file's SHA-256 `4ee384515a4325315992e61dd957f0fa91f3858fe7c079de59f56edded51de86`.

To reproduce this Linux setup, obtain that exact package from an authenticated Debian repository or snapshot, verify its recorded digest, and extract it to a new directory. Then point the compiler at the extracted package's architecture-specific headers, retaining the regular proof and its dependency tracing:

```sh
sha256sum linux-libc-dev_6.12.107-1_all.deb
# Require dc68dd53c7356598d005866da0d19d7bd33af2a62971aced624cdf04492902b4.
dpkg-deb -x linux-libc-dev_6.12.107-1_all.deb /absolute/new/path/debian-headers
CPLUS_INCLUDE_PATH=/absolute/new/path/debian-headers/usr/lib/linux/uapi/x86 \
  python3 tools/check_sdk_tutorial.py --archive /absolute/path/sco-sdk-2.4.0.zip \
  --sums /absolute/path/SHA256SUMS --work /absolute/new/path/tutorial-proof
```

The compiler's own probe reported that directory as a system include root. This is a selected, verified toolchain input, not a general authorization for plugin-provided include directories. The default image layout still needs that setup or a separately tested tracer repair. The full run and escape-test logs are local evidence, not game evidence.

The repository `tools/check.sh` passed with locally extracted, authenticated Debian Clang 19.1.7 and MinGW-w64 packages: **65 translation units, 0 diagnostics, 0 new, 0 gone**. The exact core submodule was initialized at the product's recorded `844e136ccb280ad8cb43f73bc43c9447e8f6799d`. No system package installation or elevated privilege was required.

The immutable RC2 `docs/agent-workflows.md` says RC2 was not published as of 2026-10-28; the release page now has the assets. The historical approval lookup's stable-tag restriction is intentional and is not a release blocker. The source patch here cannot change released bytes; correcting the stale packaged documentation requires a future release. Windows/MSVC, launcher discovery, actual host loading, runtime MCP, install/reload and game checks were not run. No in-game validation is established.
