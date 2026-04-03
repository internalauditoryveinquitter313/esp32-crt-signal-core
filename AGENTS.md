# AGENTS.md

Complete knowledge base for AI agents working on this repository. See also `CLAUDE.md` for a quick-start summary.

## Project Overview

ESP32 CRT Signal Core — deterministic composite video signal engine for the ESP32-D0WD-V3. Generates NTSC/PAL composite video via I2S0 + internal DAC on GPIO25. Based on the hardware strategy from `ESP_8_BIT_composite` but rebuilt from scratch with a signal-first architecture.

The realtime unit is the **scanline**, not the frame. The system synthesizes each scanline through a bounded stage pipeline, queues it ahead of DMA consumption, and outputs it via I2S-driven DAC. No framebuffer is required for basic operation.

Design spec: `docs/superpowers/specs/2026-04-02-esp32-crt-signal-core-design.md`

## Build System

**Framework:** ESP-IDF 5.4.0 with CMake  
**Target chip:** ESP32-D0WD-V3 (Xtensa dual-core)  
**Toolchain:** Xtensa GCC via ESP-IDF

```bash
# Ensure ESP-IDF is sourced (. ~/esp/esp-idf/export.sh or equivalent)

idf.py build                          # Full build
idf.py -p /dev/ttyUSB0 flash          # Flash
idf.py -p /dev/ttyUSB0 monitor        # Serial monitor (Ctrl+] to exit)
idf.py -p /dev/ttyUSB0 flash monitor  # Flash + monitor
idf.py fullclean                      # Clean build artifacts
idf.py menuconfig                     # Interactive sdkconfig editor
idf.py set-target esp32               # Set target (already configured)
```

### sdkconfig highlights
- Target: ESP32 (CONFIG_IDF_TARGET="esp32")
- Flash: 4MB DIO 40MHz
- DAC enabled, I2S enabled
- Debug optimization, assertions on
- 2-stage bootloader

## Tests

Tests are standalone C programs using `assert.h` — no test framework. Each file has `int main(void)` and can be compiled/run on the host (not the ESP32 target).

```bash
# Example: compile and run burst waveform test
gcc -I components/crt_core/include -I components/crt_timing/include \
    tests/burst_waveform_test.c components/crt_core/crt_waveform.c \
    -lm -o /tmp/burst_test && /tmp/burst_test

# Line policy test
gcc -I components/crt_core/include -I components/crt_timing/include \
    tests/line_policy_test.c components/crt_core/crt_line_policy.c \
    -o /tmp/line_policy_test && /tmp/line_policy_test

# Timing profile test
gcc -I components/crt_timing/include \
    tests/crt_timing_profile_test.c components/crt_timing/crt_timing.c \
    -o /tmp/timing_test && /tmp/timing_test

# Demo pattern test (requires -lm for math.h, needs mock-free subset of demo code)
gcc -I components/crt_demo/include -I components/crt_timing/include \
    -I components/crt_core/include \
    tests/crt_demo_pattern_test.c components/crt_demo/crt_demo_pattern.c \
    components/crt_core/crt_waveform.c -lm \
    -o /tmp/demo_test && /tmp/demo_test
```

Note: `crt_waveform.c` uses `lroundf()` from `<math.h>`, so link with `-lm`. Tests that touch ESP-IDF APIs (like `esp_check.h`) need stubs or must run on-target.

## Architecture

### Signal Pipeline (hot path)

```
┌─────────────────────────────────────────────────────┐
│  prep task (pinned to Core 1, priority 20)          │
│                                                     │
│  for each recycled DMA slot:                        │
│    1. blanking_stage  → fill line with blank level   │
│    2. sync_stage      → insert sync pulses          │
│    3. burst_stage     → insert colorburst waveform  │
│    4. active_stage    → render demo pattern/content  │
│                                                     │
│  write completed line buffer to DMA slot            │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────┐
│  DMA ring (circular linked list of lldesc_t)         │
│  I2S0 TX → DAC channel 0 → GPIO25                   │
│                                                      │
│  EOF ISR: post recycled slot index to refill_queue   │
└──────────────────────────────────────────────────────┘
```

### Component Map

```
main/app_main.c
  └── crt_core  (engine lifecycle: init/start/stop/deinit)
        ├── crt_hal      (I2S0, APLL, DAC, DMA, ISR — hardware only)
        ├── crt_timing   (NTSC/PAL profiles, line type classification)
        ├── crt_demo     (demo pattern renderer — color bars + grayscale ramp)
        ├── crt_diag     (telemetry: underruns, queue depth, prep cycles)
        └── [crt_fb]     (optional framebuffer adapter — stub/minimal)
```

### Component Details

**crt_core** (`components/crt_core/`)
- `crt_core.c` — Engine lifecycle, prep task, stage pipeline execution. Static state (`s_initialized`, `s_running`, `s_config`, `s_timing`). Contains 4 built-in stages as static functions. Prep task runs infinite loop: wait for recycled slot → fill line → repeat.
- `crt_waveform.c` — NTSC and PAL colorburst template generation. NTSC uses 4-sample repeating pattern (±amplitude at phases 0,2). PAL uses two alternating 4-sample phase patterns with `sin(45°)` ≈ 0.47140452 amplitude scaling.
- `crt_line_policy.c` — Policy functions: `sync_width` returns vsync_width for VSYNC lines, normal sync_width otherwise. `has_burst` returns false for VSYNC lines.
- `crt_stage.h` — Stage contract types: `crt_line_context_t` (immutable line metadata), `crt_line_buffer_t` (mutable sample buffer), `crt_stage_fn_t` (stage function pointer signature).

**crt_hal** (`components/crt_hal/`)
- `crt_hal.c` — Full hardware bring-up. Acquires I2S0 via `i2s_platform_acquire_occupation`. Configures APLL for precise sample rate. Allocates DMA descriptors as circular linked list. Registers IRAM ISR that posts completed slot indices to a FreeRTOS queue. Manages DAC power via `dac_ll_*` low-level API. Uses private ESP-IDF headers (`esp_private/i2s_platform.h`, `hal/i2s_ll.h`, etc).

**crt_timing** (`components/crt_timing/`)
- `crt_timing.c` — Returns static profiles for NTSC and PAL. Line type classification: NTSC active=0-239, blank=240-244,248-261, vsync=245-247. PAL active=32-271, vsync=304-311, blank=rest.

**crt_demo** (`components/crt_demo/`)
- `crt_demo_pattern.c` — Renders 256-pixel logical width patterns. Color bars use precomputed packed composite samples (4 bytes → 4 subcarrier phases per pixel). Grayscale ramp occupies bottom 32 active lines. PAL alternates even/odd bar palettes per line. Uses legacy I2S word swap (`index ^ 1`).

**crt_diag** (`components/crt_diag/`)
- `crt_diag.c` — Simple max/min tracker. Records DMA underrun count, minimum queue depth, maximum prep cycle count. No locks (single-writer from prep task, read from control path).

**crt_fb** (`components/crt_fb/`)
- `crt_fb.c` — Minimal stub. `surface_init` fills struct, `surface_deinit` zeroes it. No actual buffer allocation yet. Format: `CRT_FB_FORMAT_INDEXED8`.

### Key Constants

| Constant | Value | Location |
|----------|-------|----------|
| `CRT_CORE_SYNC_LEVEL` | `0x0000` | `crt_core.c:17` |
| `CRT_CORE_BLANK_LEVEL` | `23 << 8` (5888) | `crt_core.c:18` |
| `CRT_CORE_PREP_TASK_PRIORITY` | 20 | `crt_core.c:20` |
| `CRT_CORE_STAGE_COUNT` | 4 | `crt_core.c:21` |
| `CRT_CORE_MAX_BURST_WIDTH` | 64 samples | `crt_core.c:22` |
| `CRT_DEMO_PATTERN_LOGICAL_WIDTH` | 256 pixels | `crt_demo_pattern.h:13` |
| `CRT_DEMO_LUMA_BLACK` | 6400 | `crt_demo_pattern.c:4` |
| `CRT_DEMO_LUMA_WHITE` | 18176 | `crt_demo_pattern.c:5` |
| `CRT_DEMO_RAMP_HEIGHT_LINES` | 32 | `crt_demo_pattern.c:3` |

### NTSC Timing Profile

| Field | Value |
|-------|-------|
| sample_rate_hz | 14,318,180 (4× 3.579545 MHz colorburst) |
| total_lines | 262 |
| active_lines | 240 |
| samples_per_line | 912 |
| active_offset | 144 |
| active_width | 768 |
| sync_width | 64 |
| vsync_width | 840 |
| burst_offset | 64 |
| burst_width | 40 |

### PAL Timing Profile

| Field | Value |
|-------|-------|
| sample_rate_hz | 17,734,476 (4× 4.43361875 MHz colorburst) |
| total_lines | 312 |
| active_lines | 240 |
| samples_per_line | 1136 |
| active_offset | 184 |
| active_width | 768 |
| sync_width | 80 |
| vsync_width | 536 |
| burst_offset | 96 |
| burst_width | 44 |

## Hardware Constraints

- **GPIO25 only** — this is the ESP32's DAC channel 0. The HAL hardcodes this and rejects any other pin.
- **I2S0 exclusive** — the engine claims I2S peripheral 0 via `i2s_platform_acquire_occupation`. Cannot coexist with other I2S0 users.
- **APLL** — Audio PLL is acquired for precise sample rate. Released on shutdown.
- **DMA buffers must be internal SRAM** — `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`. External PSRAM is too slow for DMA.
- **ISR is IRAM-resident** — `crt_hal_isr` and anything it calls must be in IRAM.

## Critical Design Rules

1. **No dynamic allocation after `crt_core_start()`** — all buffers preallocated during init.
2. **ISR does minimal work** — only posts slot index to FreeRTOS queue and increments underrun counter on queue-full.
3. **Prep task pinned to Core 1** — application logic should stay on Core 0.
4. **Stage contract** — each stage receives immutable `crt_line_context_t` and mutable `crt_line_buffer_t`. Must not allocate, block, log, or touch peripherals.
5. **Signal integrity > image quality** — under timing pressure, shed optional stages but preserve sync/blanking.
6. **DMA descriptors form a circular linked list** — `descs[i].qe.stqe_next = &descs[(i+1) % count]`.
7. **I2S word swap** — demo patterns use `index ^ 1` (the `CRT_DEMO_I2S_WORD` macro) because I2S outputs 16-bit words in swapped order.
8. **PAL phase alternation** — burst and color bar palettes alternate per line (`line_index & 1`). Even lines use phase_a/even palettes, odd lines use phase_b/odd.
9. **Burst templates are precomputed** — stored in `s_burst_state` during init, memcpy'd into line buffers at runtime.

## Implementation Roadmap (from design spec)

1. ~~Sync and blanking only~~ ✓
2. ~~Active video with test patterns~~ ✓
3. ~~Stage pipeline~~ ✓
4. Telemetry and budget enforcement (basic telemetry done, budget enforcement pending)
5. Optional framebuffer adapter (stub exists)
6. Feedback and reservoir experiments
7. Compatibility wrappers

## Code Style

- C11, no C++ in component sources (headers have `extern "C"` guards)
- ESP-IDF error handling: `ESP_RETURN_ON_FALSE`, `ESP_RETURN_ON_ERROR`, `ESP_GOTO_ON_ERROR`
- Static module state (`s_` prefix) — no global singletons exposed in headers
- All public API functions return `esp_err_t` except queries (return values directly)
- Telemetry uses lock-free single-writer pattern (prep task writes, control path reads)
- Log tags match component names: `"crt_core"`, `"crt_hal"`, `"crt_timing"`, `"crt_fb"`
