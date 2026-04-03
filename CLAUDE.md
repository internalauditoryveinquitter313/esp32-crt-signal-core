# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

For complete architecture details, timing tables, component internals, and hardware constraints, see **AGENTS.md**.

## Project

ESP32 CRT Signal Core — deterministic NTSC/PAL composite video engine for ESP32-D0WD-V3. Signal-first: the scanline is the realtime unit, not the frame. Output via I2S0 + DAC on GPIO25.

## Build

Requires ESP-IDF 5.4+ sourced in shell.

```bash
idf.py build                          # Build
idf.py -p /dev/ttyUSB0 flash monitor  # Flash + serial monitor
idf.py fullclean                      # Clean
idf.py menuconfig                     # sdkconfig editor
```

## Tests

Standalone host-compiled C tests using `assert.h`. Link with `-lm` when `crt_waveform.c` is involved.

```bash
gcc -I components/crt_core/include -I components/crt_timing/include \
    tests/burst_waveform_test.c components/crt_core/crt_waveform.c \
    -lm -o /tmp/burst_test && /tmp/burst_test
```

## Architecture

```
prep task (Core 1) → [blanking → sync → burst → active] → DMA ring → I2S0 → DAC → GPIO25
                                                                  ↑
                                                          EOF ISR recycles slots
```

Components: `crt_core` (engine) → `crt_hal` (hardware) + `crt_timing` (profiles) + `crt_demo` (patterns) + `crt_diag` (telemetry) + `crt_fb` (framebuffer stub)

## Rules

- No malloc after `crt_core_start()`. All buffers preallocated.
- ISR is minimal — only posts slot index to queue.
- Prep task pinned to Core 1. App logic on Core 0.
- Stage contract: no alloc, no block, no log, no peripheral access.
- Signal integrity > image quality. Shed optional stages before losing sync.
- DMA buffers: `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` (internal SRAM only).
- GPIO25 only (DAC channel 0). HAL rejects other pins.
