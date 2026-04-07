# ESP32 CRT Signal Core Design

**Date:** 2026-04-02

**Goal**

Design a 2026-era composite video engine for the ESP32-D0WD-V3 that is optimized for low-level signal control first, while retaining an optional framebuffer path for visualization and compatibility layers later.

**Primary Target**

- Chip: `ESP32-D0WD-V3`
- Runtime: `ESP-IDF` first
- Output: composite video on `GPIO25` via internal DAC
- Priority: deterministic timing, low jitter, scanline-level control, extensibility for CRT reservoir experiments

**Guiding Decisions**

- The project is `signal-first`, not `framebuffer-first`.
- The realtime object is the scanline, not the frame.
- Arduino compatibility is optional and comes after the core is stable.
- The ISR remains minimal; line synthesis happens ahead of consumption.
- Framebuffer support is an adapter, not the architectural center.

**Why This Direction**

The legacy `ESP_8_BIT_composite` approach proved that the original ESP32 can generate composite color video using `I2S0`, `APLL`, DMA, and the built-in DAC. However, the old library is tightly coupled to obsolete assumptions and to an Arduino-era integration model. The modern design should preserve the useful hardware strategy while restructuring the system around explicit timing, explicit memory ownership, and explicit realtime contracts.

**Architecture**

1. `crt_hal`

- Owns `I2S0`, `APLL`, DAC, DMA descriptors, interrupt registration, and hardware lifecycle.
- Exposes only low-level primitives needed by the video engine.
- No graphics logic and no user callbacks.

2. `crt_video_timing`

- Encodes NTSC and PAL timing tables.
- Defines line schedule: sync, blanking, burst, active region.
- Provides immutable or precomputed timing data used by the hot path.

3. `crt_signal_core`

- Main engine API.
- Produces each scanline using a bounded stage pipeline.
- Owns stage registration, execution ordering, and per-stage budgets.

4. `crt_framebuffer_adapter`

- Optional producer of active-region samples from a framebuffer.
- Contributes only to the active window.
- Can be disabled without affecting base signal generation.

5. `crt_experiments`

- Hosts feedback passes, perturbation stages, modulation experiments, and debug overlays.
- Must obey the same bounded execution contract as the rest of the signal core.

**Execution Model**

- A high-priority prep task runs ahead of the DMA consumer and prepares a small queue of future scanlines.
- DMA consumes only line buffers that are already prepared.
- The EOF ISR only rotates ownership, advances line counters, and signals the prep pipeline.
- The system maintains headroom by keeping multiple ready lines in a queue.

**Memory Model**

Use internal SRAM deliberately. Critical line buffers and timing data must stay in memory suitable for DMA and predictable access.

- `dma line pool`
  - preallocated line buffers
  - `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`
- `signal work buffers`
  - temporary composition buffers for stages
- `timing and LUT storage`
  - precomputed sync, burst, palette, and helper tables
- `optional framebuffer`
  - independent memory block
  - removable without breaking the core

Rules:

- No dynamic allocation after video start.
- No hidden shared ownership between ISR and tasks.
- All hot-path data structures are fixed-capacity and preinitialized.

**Scheduling**

- Keep the critical prep task pinned to one core.
- Keep noncritical application logic off the core used by video prep.
- Treat Wi-Fi/Bluetooth and application conveniences as background concerns.

Roles:

- `DMA EOF ISR`: minimal bookkeeping only
- `prep task`: prepares future lines and enforces pipeline budgets
- `control task`: mode changes, diagnostics snapshots, stage enable/disable
- `app/render task`: optional producer for framebuffer or control parameters

**Stage Contract**

Each stage:

- receives a valid line buffer and immutable line context
- may modify only allowed regions
- does not allocate
- does not block
- does not log in the hot path
- does not touch peripherals directly
- has bounded and measured execution cost

Stage classes:

- `deterministic`: fixed-cost operations
- `bounded`: limited but variable-cost operations
- `experimental`: too expensive for the hard realtime path unless precomputed

Recommended execution order:

1. `sync_stage`
2. `blanking_stage`
3. `burst_stage`
4. `active_window_base_stage`
5. `framebuffer_adapter_stage` optional
6. `signal_transform_stage`
7. `feedback_stage`
8. `post_stage`

**Degradation Strategy**

When timing pressure rises:

1. disable nonessential overlays
2. reduce optional transforms
3. disable framebuffer contribution if needed
4. preserve sync, blanking, and signal integrity at all costs

The signal staying locked is more important than image richness.

**Telemetry**

Required counters:

- `dma_underrun_count`
- `ready_queue_min_depth`
- `prep_cycles_max`
- `stage_cycles_max[]`
- `degradation_events`
- `frame_jitter_stats`

Telemetry design:

- hot path records counters only
- formatting and reporting happen in a lower-priority diagnostic path
- optional on-screen debug overlay can visualize headroom and underruns

**Implementation Sequence**

1. bring up sync and blanking only
2. add active video window with synthetic test patterns
3. add `crt_signal_core` stage pipeline
4. add telemetry and budget enforcement
5. add optional framebuffer adapter
6. add feedback and reservoir experiments
7. add compatibility wrappers only after the core is proven stable

**Recommended Initial Build Profiles**

- `lab-minimal`
  - signal-first, no framebuffer
- `hybrid-default`
  - signal-first with framebuffer adapter available
- `legacy-compat`
  - adapter-heavy mode for later compatibility work

**Non-Goals For Phase 1**

- broad multi-chip support
- Arduino-first architecture
- feature-heavy graphics framework
- large UI stack integration
- aggressive API compatibility with the legacy library before proving the core

**Result**

This design keeps the strongest part of the old composite-video work, namely the hardware strategy on the original ESP32, while correcting its main architectural weakness: treating the framebuffer as the center of the system. The new core is intended to be a deterministic composite signal engine that can host framebuffer rendering when useful, without becoming dependent on it.
