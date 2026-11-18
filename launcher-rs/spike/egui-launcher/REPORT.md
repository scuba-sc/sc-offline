# P4 UI spike: egui launcher, glow vs wgpu

Status: measured on one machine. Nothing here was run in game. The window shows static fake data.

## What was built

- `eframe 0.37.0` / `egui 0.37.0`, `default-features = false`, one renderer feature per binary.
- Two pages: Play (disabled button) and Status (static fake rows, one Cyrillic line).
- Colours taken from the `kUi*` constants in `launcher/launcher.cpp`: near-black greys, text 236/236/240, green 46/196/118.
  The C++ launcher is neutral grey with green only as a status accent. The spike uses the same, so it is not a "dark green" theme.
- Release profile: `crt-static`, `opt-level = "s"`, `lto = true`, `strip`, `panic = "abort"`, `codegen-units = 1`.
- Own `Cargo.toml` and `Cargo.lock`, empty `[workspace]`, excluded from `launcher-rs` members.
- Build: `./build.sh glow` or `./build.sh wgpu`. The script sets `--remap-path-prefix` for CARGO_HOME and the spike directory, because a plain `cargo build` leaves the builder's home path in about 270 panic strings of the exe (found in review). After the remap, `strings` finds no `Users`, user name or repo path in either exe. Cargo config cannot expand CARGO_HOME, so the flags live in the script. A real launcher needs the same flags in its CI build.

## Numbers

| | glow | wgpu |
|---|---|---|
| Release exe size (built with `build.sh`) | 3,537,408 bytes (3.37 MiB) | 6,727,680 bytes (6.42 MiB) |
| Crates in `cargo tree` (windows target, normal + build, unique, incl. the spike) | 113 | 168 |
| Crates in `cargo tree` (normal only) | 105 | 162 |
| `cargo deny check` (all four: advisories, bans, licenses, sources; `deny.toml`, windows target) | all ok | all ok |
| Extra DLL imports | `OPENGL32.dll` | `opengl32.dll`, `dxgi.dll`, `setupapi.dll`, `oleaut32.dll` |
| Starts on this machine (RTX 5070 Ti) | yes, GL 3.3 NVIDIA | yes (Vulkan, Dx12 and Gl each tried) |
| Working set, one sample, normal window, 6 s after start | about 66 MB | about 154 MB (Vulkan default) |
| Starts with no GPU driver at all | not tested | not tested. Software adapter (WARP) forced on a machine that also has a GPU: renders (see below) |

`cargo deny check` (all) passes for both. Warnings only: two "license not encountered" (`Apache-2.0 WITH LLVM-exception`, `BSL-1.0`); duplicate versions of `miniz_oxide` and `windows-sys` in glow; the same plus `hashbrown`, `rustc-hash` and `syn` in wgpu. The advisory check used the advisory database available on this machine on 2026-11-17. `deny.toml` covers the windows target only; a run with no target filter (277 crates in the lock) also passed licenses in review.
The crate counts above came from `cargo tree --features <glow|wgpu> -e normal --prefix none | sort -u | wc -l` (windows and host target gave the same 105 and 162). A review run with a different unique-count method got 163 for wgpu, so treat the gap as 57 or 58 crates; glow matched at 105. Counting all targets gives 255 (glow) and 318 (wgpu), which includes Linux and macOS only crates.

Each exe was started from PowerShell, left for 6 seconds and killed. Memory is a single uncontrolled sample: it depends on window state, driver and timing (an earlier run with a minimised window gave 69 MB glow and 177 MB wgpu). Treat it as approximate; the direction (glow lower) held in every run. Window title and a screenshot of the wgpu/WARP Play page were checked; the Status page and the Cyrillic line were not looked at.

## Software rendering (not a true no-GPU test)

Neither variant was tested on a machine with no GPU driver (VM, remote desktop, or the display adapter disabled). The NVIDIA GPU stayed enabled for every run. The machine also has a second adapter (AMD, status Unknown), so "no GPU" was not reproduced here.

- wgpu: I added a test switch `SCOL_SPIKE_CPU_ADAPTER=1` that selects an adapter of type Cpu.
  Result: it picked "Microsoft Basic Render Driver / Dx12 / Cpu" (WARP) and the Play page rendered (screenshot checked). Working set about 80 to 116 MB.
  With `WGPU_BACKEND=vulkan` or `gl` and the switch, no software adapter exists and it exits with an error.
  `WGPU_FORCE_FALLBACK_ADAPTER=1` and `WGPU_ADAPTER_NAME` were ignored by egui-wgpu 0.37 (the NVIDIA adapter was still chosen), so a custom adapter selector is required.
  A plain run (no switch) chooses the GPU.
  This proves the software render path works when WARP is selected explicitly. It does not prove a start with no GPU driver, and the GPU-then-software retry does not exist in code yet. A fallback needs launcher code: try the GPU, and on failure retry with the Cpu selector.
- glow: not tested. This machine has no Mesa llvmpipe and no `opengl32sw.dll`, and I did not disable the GPU. Windows' own software OpenGL is version 1.1, and egui_glow needs a newer GL, so it most likely fails without a GPU driver. That is a reading of the docs, not a measurement.
  A bundled `opengl32sw.dll` (Mesa, MIT) would add several MB and was not tried.

## Fonts

- No font file is in the repo (`src/third_party` holds only imgui sources, `data` has none). The C++ launcher uses the ImGui built-in font.
- egui default fonts are off, so egui has no font until one is loaded. The spike reads `%SystemRoot%/Fonts/segoeui.ttf` at run time and exits with code 2 if it is missing. Nothing is embedded or redistributed.
- Decision needed before shipping: choose a font with an allowed licence and Cyrillic coverage, for example an OFL font, and add it to the repo with its licence text. Segoe UI cannot be bundled.
- The `system_fonts` feature of eframe 0.37 exists and was not tried.

## Recommendation

Use glow for the launcher, conditionally: this holds only if the no-GPU case is tested and judged acceptable. It rests on size and crate count, not on a no-GPU measurement. glow's no-GPU result is untested; wgpu's works only with custom retry code.

- Size: 3.2 MB less and about 57 fewer crates (105 vs 162, normal deps). Fewer crates to audit; the advisory and bans checks pass for both, so this is a size argument, not a finding.
- Memory: lower working set in every sample (approximate, see above).
- The launcher is a small settings and status window for players who have a GPU to run Star Citizen. wgpu's main advantage is the WARP fallback, which these players rarely need.

Choose wgpu only if a start with no GPU (remote desktop, virtual machine, broken driver) must work out of the box. It is the only variant where a software path rendered here (WARP forced explicitly), and it needs custom adapter-retry code. Windows software OpenGL is 1.1, so glow may well fail in those cases; if the glow test fails and those players matter, the verdict flips to wgpu.

Open before deciding:
1. Test glow with no GPU on a machine that has one to disable, or in a VM.
2. Pick the font.
3. Both variants lose the C++ launcher's custom drawing (icons, spinners); egui widgets would need restyling to match.
4. Not run in game; the spike has no launch logic.
5. Theme: the task asked for dark green; the spike copies the neutral grey C++ constants (green is a status accent only). Owner to say if a green theme is wanted.
6. Vendoring: not run by me. A review ran `cargo vendor --locked` on the spike: it worked, 276 crates, 446 MB, because it pulls crates for every target (Linux, macOS, others). A real vendor step needs target filtering, or a cargo-deny-gated lock, before it can be checked in or used for offline builds.
7. Advisory gate: `cargo deny check advisories` used the local database snapshot. CI should run `cargo deny fetch` first and record the database date.

## Not measured

Cold-start time, idle CPU, high-DPI behaviour, the real cost of icons and spinners, the Status page and Cyrillic line by eye, glow with no GPU, wgpu with no GPU driver, and Windows 10 behaviour. Memory is one sample per variant. The font is Segoe UI loaded at run time (Windows only, not redistributable). Nothing was run in game.
