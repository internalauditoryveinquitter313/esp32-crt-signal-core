# ESP32 CRT Signal Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a signal-first composite video engine for `ESP32-D0WD-V3` with a minimal realtime core, scanline-prep pipeline, telemetry, and an optional framebuffer adapter.

**Architecture:** The implementation is `ESP-IDF` first and centered on a bounded scanline pipeline. `crt_hal` owns hardware, `crt_video_timing` owns NTSC/PAL timing data, `crt_signal_core` owns the stage pipeline, and the framebuffer remains optional and out of the hard realtime center.

**Tech Stack:** ESP-IDF, C/C++, FreeRTOS, ESP32 `I2S0` + DMA + internal DAC, IRAM/DRAM placement, heap capability allocation.

---

### Planned File Structure

**Files:**
- Create: `main/app_main.c`
- Create: `components/crt_hal/include/crt_hal.h`
- Create: `components/crt_hal/crt_hal.c`
- Create: `components/crt_timing/include/crt_timing.h`
- Create: `components/crt_timing/crt_timing.c`
- Create: `components/crt_core/include/crt_core.h`
- Create: `components/crt_core/crt_core.c`
- Create: `components/crt_core/include/crt_stage.h`
- Create: `components/crt_fb/include/crt_fb.h`
- Create: `components/crt_fb/crt_fb.c`
- Create: `components/crt_diag/include/crt_diag.h`
- Create: `components/crt_diag/crt_diag.c`
- Create: `test_apps/pattern_bars/main/pattern_bars.c`
- Create: `test_apps/signal_only_sync/main/signal_only_sync.c`

### Task 1: Repository Skeleton

**Files:**
- Create: `components/crt_hal/include/crt_hal.h`
- Create: `components/crt_timing/include/crt_timing.h`
- Create: `components/crt_core/include/crt_core.h`
- Create: `components/crt_diag/include/crt_diag.h`
- Create: `main/app_main.c`

- [ ] **Step 1: Create the component directory structure**

Create the `components/crt_hal`, `components/crt_timing`, `components/crt_core`, `components/crt_fb`, and `components/crt_diag` trees with `include/` directories.

- [ ] **Step 2: Add minimal public headers**

Define only the core opaque handles, config structs, and start/stop prototypes. Do not add framebuffer-first APIs.

- [ ] **Step 3: Add a minimal `app_main`**

Create a startup file that initializes the future runtime and can later be switched between test modes.

- [ ] **Step 4: Build and verify the skeleton compiles**

Run: `idf.py build`
Expected: project builds with empty stubs and no unresolved symbols.

- [ ] **Step 5: Commit**

```bash
git add main components
git commit -m "chore: scaffold esp32 crt signal core layout"
```

### Task 2: HAL Bring-Up For Sync And Blanking

**Files:**
- Create: `components/crt_hal/crt_hal.c`
- Modify: `components/crt_hal/include/crt_hal.h`
- Modify: `main/app_main.c`
- Test: `test_apps/signal_only_sync/main/signal_only_sync.c`

- [ ] **Step 1: Define the HAL configuration**

Include DMA line pool counts, sample rate selection, NTSC/PAL mode, GPIO selection fixed to DAC-capable pins, and ISR hook registration.

- [ ] **Step 2: Implement hardware init and shutdown**

Set up `I2S0`, APLL selection, internal DAC output, DMA descriptors, and cleanup paths.

- [ ] **Step 3: Implement a minimal EOF ISR**

The ISR should acknowledge interrupt state, rotate line ownership, increment counters, and signal the prep task. No waveform generation.

- [ ] **Step 4: Emit sync + blanking only**

Prepare a constant black/sync signal and verify the CRT locks onto a stable image.

- [ ] **Step 5: Run hardware smoke test**

Run: `idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.defaults flash monitor`
Expected: CRT locks to a black image with stable sync and no DMA underrun spam.

- [ ] **Step 6: Commit**

```bash
git add main components test_apps
git commit -m "feat: bring up crt hal with sync and blanking output"
```

### Task 3: Timing Tables And Scanline Context

**Files:**
- Create: `components/crt_timing/crt_timing.c`
- Modify: `components/crt_timing/include/crt_timing.h`
- Modify: `components/crt_core/include/crt_core.h`

- [ ] **Step 1: Define timing data structures**

Represent per-line mode, sync widths, burst region, active offset, active width, and frame line count.

- [ ] **Step 2: Implement NTSC timing tables first**

Start with the primary target path and avoid solving PAL before NTSC is stable.

- [ ] **Step 3: Build a `crt_line_context_t`**

Make the context explicit so every future stage gets the same view of the line.

- [ ] **Step 4: Add unit-testable helpers**

Write pure functions for timing lookups and active-window metadata where possible.

- [ ] **Step 5: Run build verification**

Run: `idf.py build`
Expected: timing module compiles and exports clean interfaces without hardware dependencies.

- [ ] **Step 6: Commit**

```bash
git add components/crt_timing components/crt_core
git commit -m "feat: add ntsc timing tables and scanline context"
```

### Task 4: Scanline Prep Pipeline

**Files:**
- Create: `components/crt_core/crt_core.c`
- Create: `components/crt_core/include/crt_stage.h`
- Modify: `components/crt_core/include/crt_core.h`
- Modify: `main/app_main.c`

- [ ] **Step 1: Implement fixed-capacity line queues**

Use preallocated ownership states such as free, filling, ready, active, recycle.

- [ ] **Step 2: Add prep task pinned to one core**

The task must keep multiple ready lines ahead of DMA consumption.

- [ ] **Step 3: Implement built-in base stages**

Create `sync_stage`, `blanking_stage`, `burst_stage`, and `active_window_base_stage`.

- [ ] **Step 4: Connect prep pipeline to the HAL**

Feed ready line buffers to DMA and recycle consumed buffers safely.

- [ ] **Step 5: Run timing stress test**

Run: `idf.py flash monitor`
Expected: stable output with no visible sync loss and counters showing positive queue headroom.

- [ ] **Step 6: Commit**

```bash
git add components/crt_core main
git commit -m "feat: add scanline prep pipeline and base stages"
```

### Task 5: Telemetry And Budget Enforcement

**Files:**
- Create: `components/crt_diag/crt_diag.c`
- Modify: `components/crt_diag/include/crt_diag.h`
- Modify: `components/crt_core/crt_core.c`
- Modify: `components/crt_hal/crt_hal.c`

- [ ] **Step 1: Add hot-path counters**

Record `dma_underrun_count`, `ready_queue_min_depth`, line prep maxima, and stage maxima.

- [ ] **Step 2: Add cycle sampling wrappers**

Wrap stage execution with lightweight cycle accounting.

- [ ] **Step 3: Add degradation policy hooks**

When a stage repeatedly exceeds budget, disable optional stages before the signal path is endangered.

- [ ] **Step 4: Add a snapshot API**

Export diagnostic snapshots through a lower-priority task-safe interface.

- [ ] **Step 5: Verify under stress**

Expected: counters move as expected and no logging appears from ISR context.

- [ ] **Step 6: Commit**

```bash
git add components/crt_diag components/crt_core components/crt_hal
git commit -m "feat: add diagnostics and realtime budget enforcement"
```

### Task 6: Synthetic Signal Stages And Pattern Generator

**Files:**
- Modify: `components/crt_core/crt_core.c`
- Create: `test_apps/pattern_bars/main/pattern_bars.c`

- [ ] **Step 1: Add deterministic active-region pattern stages**

Implement color bars, checkerboard, line ramps, and burst-aligned test content without framebuffer dependency.

- [ ] **Step 2: Expose a simple stage registration API**

Allow deterministic and bounded stages to be installed in order.

- [ ] **Step 3: Add pattern demo app**

Use synthetic stages only to prove the engine works before introducing framebuffer complexity.

- [ ] **Step 4: Run hardware validation**

Expected: visible patterns are stable and stage timings remain within budget.

- [ ] **Step 5: Commit**

```bash
git add components/crt_core test_apps/pattern_bars
git commit -m "feat: add synthetic scanline stages and pattern generator"
```

### Task 7: Optional Framebuffer Adapter

**Files:**
- Create: `components/crt_fb/crt_fb.c`
- Create: `components/crt_fb/include/crt_fb.h`
- Modify: `components/crt_core/crt_core.c`
- Modify: `components/crt_core/include/crt_core.h`

- [ ] **Step 1: Define framebuffer adapter contract**

The adapter may write only the active region and must not control sync or blanking.

- [ ] **Step 2: Add a compact framebuffer format first**

Start with one format such as indexed 8-bit or packed grayscale before considering larger formats.

- [ ] **Step 3: Convert framebuffer to active-line samples**

Do all expansion outside the ISR and within prep-task budgets.

- [ ] **Step 4: Add a frame-wait helper**

Support synchronization for rendering code without making the core framebuffer-dependent.

- [ ] **Step 5: Verify performance envelope**

Expected: framebuffer mode works without threatening sync integrity; if overloaded, the degradation path triggers before underruns.

- [ ] **Step 6: Commit**

```bash
git add components/crt_fb components/crt_core
git commit -m "feat: add optional framebuffer adapter"
```

### Task 8: Reservoir And Feedback Experiments

**Files:**
- Modify: `components/crt_core/crt_core.c`
- Create: `components/crt_core/include/crt_stage.h`
- Create: `test_apps/reservoir_feedback/main/reservoir_feedback.c`

- [ ] **Step 1: Add bounded feedback state support**

Support line-to-line and frame-to-frame state handoff with explicit memory ownership.

- [ ] **Step 2: Implement one bounded feedback transform**

Choose a simple experiment such as previous-line weighted blend or thresholded decay.

- [ ] **Step 3: Add guardrails**

Refuse stage configurations that exceed declared budgets or illegal region access.

- [ ] **Step 4: Run experimental validation**

Expected: feedback effects appear while sync stays locked and counters remain intelligible.

- [ ] **Step 5: Commit**

```bash
git add components/crt_core test_apps/reservoir_feedback
git commit -m "feat: add bounded reservoir feedback experiments"
```

### Task 9: Legacy And Ergonomic Adapters

**Files:**
- Create: `components/crt_compat/include/crt_compat.h`
- Create: `components/crt_compat/crt_compat.c`
- Modify: `components/crt_fb/include/crt_fb.h`

- [ ] **Step 1: Add a thin compatibility layer**

Expose convenience helpers such as backbuffer access and frame wait without leaking old architecture into the core.

- [ ] **Step 2: Keep the adapter out of the hot path**

All compatibility code must sit above `crt_core` and never own signal timing.

- [ ] **Step 3: Verify no regression**

Expected: compatibility helpers work, and core timings do not change when the adapter is unused.

- [ ] **Step 4: Commit**

```bash
git add components/crt_compat components/crt_fb
git commit -m "feat: add optional compatibility adapter"
```

### Final Verification

- [ ] **Step 1: Run full build**

Run: `idf.py build`
Expected: all components compile cleanly.

- [ ] **Step 2: Run sync-only smoke test**

Expected: CRT locks to stable blank video.

- [ ] **Step 3: Run pattern generator test**

Expected: stable visible test patterns with no critical underruns.

- [ ] **Step 4: Run framebuffer test**

Expected: adapter mode works within budget or degrades safely.

- [ ] **Step 5: Run reservoir feedback demo**

Expected: feedback stages remain bounded and do not break signal integrity.
