# AGENTS.md

Shared repository instructions for coding agents. Codex reads this file directly. `CLAUDE.md` and `GEMINI.md` import it so the repo has one canonical agent guide.

## Project Snapshot

- Project: ESP32 CRT Signal Core
- Goal: deterministic NTSC/PAL composite video engine for ESP32-D0WD-V3
- Output path: `I2S0 -> internal DAC -> GPIO25`
- Realtime unit: scanline, not frame
- Design center: signal-first core with optional framebuffer and hook-based rendering
- Research direction: `docs/research/tcbvn.md`

## Instruction Model

- Keep shared agent guidance in this file.
- Keep tool-specific entrypoints thin: `CLAUDE.md` and `GEMINI.md` should mostly import this file.
- Do not duplicate operational guidance across multiple root instruction files.
- When changing architecture, workflow, or verification rules here, also update any affected user-facing docs such as `README.md`.

## Working Agreements

- Preserve deterministic signal behavior over image richness.
- Treat sync/blanking correctness as higher priority than optional rendering features.
- Avoid allocations after `crt_core_start()`.
- Do not add blocking work, logging, or peripheral access to hot-path stages or hooks.
- Keep ISR work minimal.
- Prefer focused edits over broad refactors.
- This repo may be dirty; never revert unrelated user changes.

## Environment

- Framework: ESP-IDF 5.4.x with CMake
- Chip: `ESP32-D0WD-V3`
- Toolchain: Xtensa GCC via ESP-IDF
- Shell note: in this environment `export.sh` must be invoked through `bash -c`, not directly from `zsh`

## Core Commands

```bash
# Build / flash / config
bash -c '. ~/esp/esp-idf/export.sh && idf.py build'
bash -c '. ~/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 flash monitor'
bash -c '. ~/esp/esp-idf/export.sh && idf.py fullclean'
bash -c '. ~/esp/esp-idf/export.sh && idf.py menuconfig'
bash -c '. ~/esp/esp-idf/export.sh && idf.py set-target esp32'

# Host tests
make test
make test-core
make test-render

gcc -I components/crt_core/include -I components/crt_timing/include \
    tests/burst_waveform_test.c components/crt_core/crt_waveform.c \
    -lm -o /tmp/burst_test && /tmp/burst_test

gcc -I components/crt_core/include -I components/crt_timing/include \
    tests/line_policy_test.c components/crt_core/crt_line_policy.c \
    -o /tmp/line_policy_test && /tmp/line_policy_test

gcc -I components/crt_timing/include \
    tests/crt_timing_profile_test.c components/crt_timing/crt_timing.c \
    -o /tmp/timing_test && /tmp/timing_test

gcc -I components/crt_demo/include -I components/crt_timing/include \
    -I components/crt_core/include \
    tests/crt_demo_pattern_test.c components/crt_demo/crt_demo_pattern.c \
    components/crt_core/crt_waveform.c -lm \
    -o /tmp/demo_test && /tmp/demo_test

gcc -I components/crt_hal/include \
    tests/crt_hal_clock_test.c components/crt_hal/crt_hal_clock.c \
    -o /tmp/crt_hal_clock_test && /tmp/crt_hal_clock_test

gcc -I components/crt_core/include -I components/crt_timing/include \
    -I tests/stubs tests/crt_composite_palette_test.c \
    components/crt_core/crt_composite_palette.c \
    -o /tmp/crt_composite_palette_test && /tmp/crt_composite_palette_test

gcc -I components/crt_core/include -I components/crt_timing/include \
    -I tests/stubs tests/crt_scanline_abi_test.c \
    -o /tmp/scanline_abi_test && /tmp/scanline_abi_test

gcc -I components/crt_core/include -I components/crt_timing/include \
    -I tests/stubs tests/crt_scanline_header_test.c \
    -o /tmp/scanline_header_test && /tmp/scanline_header_test

gcc -I components/crt_fb/include -I components/crt_core/include \
    -I components/crt_timing/include -I tests/stubs \
    tests/crt_fb_test.c components/crt_fb/crt_fb.c \
    components/crt_core/crt_composite_palette.c \
    -o /tmp/crt_fb_test && /tmp/crt_fb_test

gcc -I components/crt_compose/include -I components/crt_core/include \
    -I components/crt_timing/include -I tests/stubs \
    tests/crt_compose_test.c components/crt_compose/crt_compose.c \
    components/crt_compose/crt_compose_layers.c components/crt_compose/crt_sprite.c \
    -o /tmp/crt_compose_test && /tmp/crt_compose_test

gcc -I components/crt_stimulus/include -I components/crt_compose/include \
    -I components/crt_core/include -I components/crt_timing/include \
    -I tests/stubs tests/crt_stimulus_test.c \
    components/crt_stimulus/crt_stimulus.c \
    -o /tmp/crt_stimulus_test && /tmp/crt_stimulus_test

gcc -I components/crt_tile/include -I components/crt_core/include \
    -I components/crt_timing/include -I tests/stubs \
    tests/crt_tile_test.c components/crt_tile/crt_tile.c \
    -o /tmp/crt_tile_test && /tmp/crt_tile_test

# Tooling
(cd tools/crt_monitor && make)
python tools/img2fb.py <input_image> <output.h> <variable_name>
```

## Verification Matrix

- Changes under `components/crt_timing/`: run `crt_timing_profile_test`
- Changes under `components/crt_core/crt_waveform.c`: run `burst_waveform_test` and `crt_demo_pattern_test`
- Changes under `components/crt_core/crt_line_policy.c`: run `line_policy_test`
- Changes under `components/crt_core/crt_composite_palette.c`: run `crt_composite_palette_test` and `crt_fb_test`
- Changes under `components/crt_hal/crt_hal_clock.c`: run `crt_hal_clock_test` and `idf.py build`
- Changes under `components/crt_core/include/crt_scanline.h` or hook ABI: run `crt_scanline_abi_test` and `crt_scanline_header_test`
- Changes under `components/crt_fb/`: run `crt_fb_test`
- Changes under `components/crt_compose/`: run `crt_compose_test`
- Changes under `components/crt_stimulus/`: run `crt_stimulus_test`
- Changes under `components/crt_tile/`: run `crt_tile_test`
- Changes under `main/`, `components/crt_hal/`, `components/crt_core/crt_core.c`, or Kconfig/build wiring: run `idf.py build`
- Changes under `tools/crt_monitor/`: run `make` in `tools/crt_monitor`
- Documentation-only changes: verify for drift against current code paths and commands; update `README.md` if agent docs changed operational behavior

## Repository Map

- `main/app_main.c`: sample app wiring, framebuffer path, runtime demo behavior
- `components/crt_core/`: engine lifecycle, prep task, stage pipeline, hook dispatch, waveform helpers
- `components/crt_hal/`: I2S0, DAC, DMA, ISR, APLL, hardware lifecycle
- `components/crt_timing/`: NTSC/PAL timing profiles and line classification
- `components/crt_demo/`: demo pattern synthesis
- `components/crt_diag/`: telemetry counters and snapshots
- `components/crt_fb/`: indexed-8 framebuffer surface, palette LUT, scanline hook, compose layer adapter
- `components/crt_compose/`: per-scanline indexed-8 compositor, layer z-order + keyed transparency, palette LUT + I2S word-swap output
- `components/crt_compose/crt_sprite.*`: atlas-backed OAM sprite layer with deterministic per-scanline cap
- `components/crt_stimulus/`: deterministic measurement patterns for capture/reservoir experiments
- `components/crt_tile/`: PPU-style tilemap backend (8x8 indexed-8 patterns + nametable + scroll), fast 256→768 expansion path, fused scanline hook for compose delegation
- `tests/`: host-compiled assertion-based tests plus ESP-IDF stubs
- `tools/crt_monitor/`: webcam-backed monitoring dashboard
- `tools/img2fb.py`: image-to-framebuffer conversion helper
- `docs/research/`: research direction and physical-computing notes

## Architecture Invariants

- The prep task owns scanline synthesis ahead of DMA consumption.
- The DMA ring is circular and continuously recycled by EOF interrupts.
- The active path is selected per line:
  - fast mono path when hooks are absent and demo mode allows prerendered templates
  - hook path when frame, scanline, or mod hooks are registered
  - standard staged path otherwise
- Scanline hook replaces the demo active stage for active lines.
- Mod hook runs after composition on the full line buffer.
- Frame hook runs at frame boundary.
- `crt_scanline_t` is read-only metadata passed into hooks.

## Hardware Constraints

- GPIO output is fixed to `GPIO25` because the design uses DAC channel 0.
- `I2S0` is exclusive while the engine owns it.
- DMA buffers must live in internal SRAM with DMA capability.
- ISR-resident code must stay IRAM-safe.
- I2S output uses swapped 16-bit word ordering inside 32-bit DMA words.
- PAL handling depends on per-line phase alternation.

## Kconfig Surface

- `CRT_VIDEO_STANDARD`: selects NTSC or PAL timing
- `CRT_ENABLE_COLOR`: enables chroma burst and color demo output
- `CRT_RENDER_MODE`: selects compositor demo, direct RGB332 framebuffer, or measurement stimulus
- `CRT_ENABLE_UART_UPLOAD`: enables experimental UART0 raw framebuffer upload
- `CRT_TEST_STANDARD_TOGGLE`: auto-toggle NTSC/PAL at runtime
- `CRT_TEST_STANDARD_TOGGLE_INTERVAL_S`: toggle interval in seconds

## Agent Pitfalls

- Do not assume framebuffer support is a stub; `crt_fb` is implemented and tested.
- Do not trust stale counts, status tables, or roadmap summaries in old docs without checking the tree.
- Do not teach `idf.py` invocation patterns that contradict the shell reality in this repo.
- Do not turn the framebuffer into the architectural center; it is an adapter.
- Do not add new root-level instruction files that duplicate this one.

## Done Means

- The relevant verification commands for the touched area were run successfully.
- The changed docs reflect current code and command reality.
- No new instruction drift was introduced between `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, and `README.md`.

## Related Docs

- `.github/README.md`: human-facing overview and usage
- `docs/research/tcbvn.md`: research direction for CRT reservoir and analog/vacuum neuromorphic exploration
