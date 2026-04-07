# PhosphorOS 8-bit CRT Compositor Design

**Date:** 2026-04-04  
**Status:** Draft

## Goal

Define an implementable PhosphorOS compositor layer above the existing CRT signal core so the repository can move from demo-pattern generation to authored 8-bit scenes without abandoning the signal-first architecture.

PhosphorOS is explicitly hybrid:

- the **ESP32 renders** deterministic scanlines in bounded time
- the **host compiles** assets, scene packages, calibration artifacts, and analysis outputs

The central design insight is that a scanline is not treated as a row of pixels. It is treated as a **bounded resonant transaction**:

- `sync` is the phase anchor
- `burst` is the chroma anchor
- `active` is the energy budget

The compositor therefore thinks in **transition budgets**, not in unconstrained per-pixel drawing.

## Existing Integration Surface

This design is grounded in the current codebase and does not require replacing the signal core.

### `crt_scanline.h`

`crt_scanline_t` already provides the runtime descriptor needed by the compositor:

- `physical_line`: current physical line
- `logical_line`: visible line or `CRT_SCANLINE_LOGICAL_LINE_NONE`
- `type`: active, blank, vsync
- `frame_number`: stable frame scheduling key
- `subcarrier_phase`: Q20 phase anchor for chroma-aware effects
- `timing`: active profile and dimensions

The current hook ABI is the first integration target:

- `crt_frame_hook_fn`
- `crt_scanline_hook_fn`
- `crt_mod_hook_fn`

### `crt_fb.h`

`crt_fb_surface_t` already defines an indexed 8-bit surface with a `uint16_t` palette. PhosphorOS uses this as:

- a compatibility surface for bring-up and tests
- a debug mirror for line output validation
- the narrowest palette contract that all runtime paths must be able to export

### `crt_stage.h`

`crt_line_context_t`, `crt_line_buffer_t`, and `crt_stage_fn_t` already encode the hot-path discipline:

- immutable line metadata
- mutable sample buffer
- no allocation, blocking, logging, or peripheral access

PhosphorOS must obey those same rules even when attached through hooks rather than as a registered stage.

## Design Principles

1. **Signal-first**
   PhosphorOS composes only the active region by default. Sync, porch, and burst remain owned by `crt_core`.

2. **Transition-budgeted**
   A scene is accepted only if its compiled scanlines fit bounded per-line transition budgets.

3. **Indexed 8-bit authoring**
   Author-facing content remains tiles, sprites, text cells, masks, and palette indices.

4. **Host-compiled, device-rendered**
   Floating-point fitting, asset packing, and experiment analysis happen on the host. The ESP32 consumes compact fixed-point data.

5. **Calibration-owned palette**
   Runtime palette output is derived from measured CRT-camera response, not a guessed gamma curve.

6. **Research hooks are first-class**
   Calibration, analysis, and feedback loops are part of the design, but never run in the hard realtime path.

## Scope

### In scope

- 8-bit scene composition for active video
- tile, sprite, text, mask, and line-attribute primitives
- gamma-corrected palette generation from measured calibration
- scanline-hook integration with the existing core
- optional full-line modulation through `crt_mod_hook_fn`
- host-side asset compiler and calibration pipeline
- R-based temporal and fit analysis

### Out of scope for the first implementation

- replacing `crt_hal`, `crt_timing`, or `crt_core`
- dynamic allocation after `crt_core_start()`
- runtime camera processing on the ESP32
- unbounded scene graphs or desktop-style retained UI
- AI or host inference in the prep loop

## Architecture

### Module split

Recommended initial layout:

- `components/phosphor_scene/`
  Author-facing scene state, layer stack, and fixed-capacity runtime objects.
- `components/phosphor_compose/`
  Scanline compositor, transition accounting, and hook entry points.
- `components/phosphor_palette/`
  Runtime palette banks plus calibration-derived gamma LUT application.
- `components/phosphor_assets/`
  Compact tile, sprite, glyph, and palette blob loading on the ESP32.
- `tools/phosphor_compiler/`
  Host compiler for assets and scene packages.
- `tools/calibration/`
  Host capture orchestration, step-response fitting, and artifact generation.
- `tools/analysis/`
  Existing Python and R scripts extended for PhosphorOS metrics and reports.

### Runtime shape

The first shipping integration should use the current hook path:

1. `crt_core` runs built-in blanking, sync, and burst stages.
2. `phosphor_frame_hook()` advances frame schedules and swaps scene snapshots.
3. `phosphor_scanline_hook()` renders active content into `active_buf`.
4. `phosphor_mod_hook()` optionally applies waveform-aware post effects.

This keeps PhosphorOS additive. No core scheduler rewrite is required for the first milestone.

### Evolution path

The first compositor milestone should stay hook-based because that matches the current `crt_core` integration surface. A later refactor may move PhosphorOS behind a dedicated compose stage that follows `crt_stage_fn_t`, but only after the hook path has proven the scene model, budgets, and calibration pipeline.

### Data ownership

- Host tools own source assets, compiler outputs, calibration captures, and analysis reports.
- ESP32 owns only fixed-capacity runtime scene state, line scratch buffers, palette banks, and decoded asset blobs.
- No host-originating structure may require heap growth in the hot path.

## Scanline Transaction Model

Each physical line is modeled as a transaction with three anchors:

1. **Phase anchor**
   Established by sync timing and exposed to PhosphorOS through `crt_scanline_t.subcarrier_phase`.

2. **Chroma anchor**
   Established by burst placement and phase convention for the current standard.

3. **Energy budget**
   The active window is the only region where PhosphorOS may spend transitions.

The compositor does not ask “which pixel is here?” first. It asks:

- what states must exist on this line
- where are the state transitions
- what is the cost of those transitions
- which optional transitions may be dropped if the line budget is exceeded

That produces a line program that is closer to spans and edges than to a naive framebuffer walk.

## Scene Model

PhosphorOS uses a fixed-capacity scene snapshot that is stable for one frame and consumed scanline-by-scanline.

### Logical space

Recommended initial logical format:

- logical width: `256`
- logical height: `240`
- tile size: `8x8`
- text cell size: `8x8`

This aligns with the current demo assumptions, the existing active-line count, and simple nearest-neighbor expansion into the active sample window.

Recommended default sample mapping:

- when `active_width == 768`, treat one logical column as `3` active samples
- otherwise derive a fixed-point step exactly as `crt_fb_scanline_hook()` already does

This keeps the authored scene stable while preserving compatibility with both NTSC and PAL timing profiles.

### Scene object

Recommended runtime object:

```c
typedef struct {
    uint16_t logical_width;
    uint16_t logical_height;
    uint8_t clear_index;
    uint8_t background_bank;
    phosphor_tilemap_t bg_tilemap;
    phosphor_tilemap_t fg_tilemap;
    phosphor_text_plane_t text;
    phosphor_sprite_list_t sprites;
    phosphor_line_attr_table_t line_attrs;
    phosphor_mask_layer_t mask;
    phosphor_palette_bank_set_t palettes;
    phosphor_fx_schedule_t fx;
} phosphor_scene_t;
```

The exact type names can change, but the ownership split should not.

### Primitives

#### Tilemap

Initial tilemap contract:

- 8x8 indexed tiles
- fixed map dimensions
- tile index + palette bank + flip bits
- optional per-tile priority bit

Tilemaps are the cheapest primitive and should be the first background path.

#### Sprite

Initial sprite contract:

- indexed bitmap sprite
- `x`, `y`, `w`, `h`
- palette bank
- transparent index `0`
- priority
- horizontal and vertical flip
- optional line clip enable

Recommended first limits:

- `64` sprites per frame
- `16` visible sprites per line after clipping

Per-line sprite selection must be deterministic and bounded. Over-budget sprites are dropped by policy, not by accidental overrun.

#### Text plane

Initial text contract:

- fixed 8x8 glyphs
- 32 columns x 30 rows at `256x240`
- foreground index
- background index
- optional blink / inverse flags resolved in `phosphor_frame_hook()`

Text is intentionally a first-class primitive rather than “sprites for letters”.

#### Line attributes

Per-line attributes let PhosphorOS remain scanline-native. Initial fields should include:

- horizontal scroll offset
- palette override bank
- enable/disable procedural layer
- modulation preset id
- mask region for that line

Line attributes are cheap to apply and make the scene model honest about scanline variation.

## Palette Model

### Authoring palette

PhosphorOS remains 8-bit indexed at authoring time. The first implementation should support a logical master palette of `256` entries arranged as banks.

Recommended bank model:

- global entry count: `256`
- bank size: `16`
- bank count: `16`

Primitives carry bank selection plus local color index. This matches common 8-bit workflows while keeping runtime decode simple.

### Runtime palette

The current `crt_fb_surface_t.palette` stores `uint16_t` DAC levels. PhosphorOS must preserve the ability to export a banked logical palette into that format for grayscale and compatibility paths.

Recommended runtime palette entry:

```c
typedef struct {
    uint16_t mono_dac_level;
    uint16_t gamma_dac_level;
    uint8_t chroma_class;
    int8_t phase_offset_q7;
    uint8_t reserved;
} phosphor_palette_entry_t;
```

Interpretation:

- `mono_dac_level`: direct compatibility with `crt_fb_surface_t.palette`
- `gamma_dac_level`: measured luma target after calibration
- `chroma_class` and `phase_offset_q7`: reserved for phase-aware color paths

The first milestone may use only the luma fields while still defining the richer entry layout.

### Gamma from calibration

Palette output must be derived from measured step response, not from a hardcoded 2.2 assumption.

Required calibration outputs:

- `black_level_dac`
- `white_level_dac`
- `gamma_estimate`
- `luma_lut[256]`
- `palette_lut[256]`

`palette_lut[256]` is the direct bridge into `crt_fb_surface_t.palette` and the simplest first-runtime integration target.

## Transition Budget Model

### Why transitions are the unit

In composite video, the expensive part is not the abstract existence of pixels. It is the number, size, and timing of transitions the signal is asked to make inside the active window.

PhosphorOS therefore compiles each active line into a bounded line program made of runs and transitions.

### Line program

Recommended first representation:

```c
typedef struct {
    uint16_t x_start;
    uint16_t x_end;
    uint8_t palette_entry;
    uint8_t flags;
} phosphor_span_t;

typedef struct {
    uint8_t span_count;
    uint8_t transition_cost;
    uint8_t effect_cost;
    uint8_t dropped_flags;
    phosphor_span_t spans[PHOSPHOR_MAX_SPANS_PER_LINE];
} phosphor_line_program_t;
```

The key invariant is that `span_count` and `transition_cost` are bounded before rasterization begins.

### Budget terms

Each line has a compile-time and runtime budget with at least these components:

- `span budget`: number of constant-state spans
- `edge budget`: number of palette transitions
- `effect budget`: optional post-compose work
- `overlap budget`: number of resolved primitive overlaps

Recommended first cost model:

- entering a new span: `+1`
- changing palette entry between adjacent spans: `+1`
- sprite-over-background boundary: `+1`
- text foreground/background toggle: `+1`
- modulation region enable: `+2`
- reserved chroma phase discontinuity: `+2`

The exact coefficients can be tuned, but the accounting must stay integer and deterministic.

### Budget enforcement

Budget enforcement happens in two places:

1. **Host compiler**
   Reject or warn on scenes that exceed configured caps in the worst case.

2. **ESP32 runtime**
   Apply deterministic degradation if the current line exceeds runtime caps.

Recommended degradation order:

1. drop optional modulation
2. drop procedural layer
3. clamp visible sprites per line
4. collapse text attributes to a cheaper preset
5. keep background tilemap and legible text if possible

Sync and burst are never degraded because they remain below the compositor.

## Rendering Pipeline

### Frame hook

`phosphor_frame_hook(uint32_t frame_number, void *user_data)` should:

- advance animation counters
- resolve blinking and frame-rate-divided text attributes
- swap front/back scene snapshot pointers
- latch calibration or feedback parameter updates produced by the host/control path

It must not allocate or perform analysis work.

### Scanline hook

`phosphor_scanline_hook(const crt_scanline_t *scanline, uint16_t *active_buf, uint16_t active_width, void *user_data)` is the primary compositor entry point.

For an active line it should:

1. map `logical_line` to the scene’s line state
2. gather the tile row, visible sprite fragments, text row, and mask state
3. build a bounded `phosphor_line_program_t` in fixed scratch memory
4. enforce transition budget
5. rasterize spans into `active_buf`

For non-visible lines it does nothing, matching the current `crt_fb_scanline_hook()` behavior.

### Modulation hook

`phosphor_mod_hook()` is reserved for full-line or waveform-aware work that needs:

- `subcarrier_phase`
- whole-line context
- porch-relative or burst-relative perturbation

It should remain optional. The first compositor milestone can ship with `phosphor_mod_hook()` disabled by default.

## Relationship to `crt_fb_surface_t`

PhosphorOS is not framebuffer-first, but `crt_fb_surface_t` remains useful in three concrete ways:

1. **Compatibility mode**
   A scene can be rendered into a `CRT_FB_FORMAT_INDEXED8` surface and then emitted using the existing `crt_fb_scanline_hook()` for a low-risk grayscale path.

2. **Debug mirror**
   The compositor can maintain a host-visible or test-visible 8-bit mirror to compare line-program output against expected indices.

3. **Bring-up tests**
   Host tests can validate palette generation, glyph decode, tile decode, and transition accounting without requiring full signal synthesis.

The spec therefore treats `crt_fb_surface_t` as an adapter and test surface, not as the architectural center.

## Host Compiler

The host compiler is the other half of the hybrid model.

### Responsibilities

- convert source sprites, tiles, and fonts into compact indexed blobs
- pack palette banks
- validate scene limits against configured runtime budgets
- emit runtime scene packages with fixed-capacity headers
- optionally precompute span-friendly metadata for expensive assets

### Outputs

Recommended artifacts:

- `scene.pho`
  Scene package with tilemaps, sprites, text data, bank layout, and limits.
- `palette.pho`
  Logical palette plus calibration-derived DAC LUT.
- `calibration.json`
  Human-readable calibration metadata and confidence values.

The compiler may also emit C headers for deeply embedded builds, but the format should remain schema-driven rather than ad hoc.

## Calibration Module

### Objective

Generate a measured mapping from authored 8-bit indices to visible CRT output, with enough metadata to make the palette and geometry trustworthy.

### Calibration mode

PhosphorOS should provide a dedicated calibration scene with:

- border fiducials for crop and perspective solve
- center crosshair
- grayscale step wedge
- black-to-white edge target for step response
- line-pair chart
- optional chroma stripe targets for later color calibration

This scene is emitted by the ESP32 using the same compositor path as ordinary content. Calibration is not a separate rendering engine.

### Semi-automatic webcam flow

The intended first workflow:

1. user positions a webcam in front of the CRT
2. ESP32 emits the calibration scene
3. host capture tool grabs stills or a short video burst
4. Python analysis detects fiducials automatically
5. if confidence is low, the tool asks the user to confirm corners or crop
6. the tool extracts geometry, black/white levels, and edge step response
7. gamma and LUT artifacts are written for the runtime

This is intentionally **semi-automatic**:

- geometry solve should default to automatic detection
- human confirmation is allowed when the optical setup is messy
- the calibration tool must report confidence rather than silently assuming success

### Calibration outputs

Required fields in `calibration.json`:

```json
{
  "video_standard": "NTSC",
  "capture_device": "/dev/video0",
  "timestamp_utc": "2026-04-04T00:00:00Z",
  "geometry": {
    "homography": [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0],
    "crop": [0, 0, 0, 0]
  },
  "levels": {
    "black_level_dac": 5888,
    "white_level_dac": 18176,
    "gamma_estimate": 2.05
  },
  "luts": {
    "palette_lut_path": "palette_lut.bin",
    "gamma_lut_path": "gamma_lut.bin"
  },
  "metrics": {
    "edge_overshoot_pct": 0.0,
    "halo_radius_px": 0.0,
    "frame_drift_px": 0.0,
    "fit_rmse": 0.0
  }
}
```

The exact schema may evolve, but these categories are required.

## Analysis Pipeline

### Python analysis

The existing `tools/analysis/analyze_crt_capture.py` should be extended rather than replaced.

New PhosphorOS responsibilities for the Python tool:

- calibration scene detection
- fiducial solve
- edge spread and step response extraction
- LUT proposal generation
- JSON and CSV output for downstream analysis

### R integration

R is the reporting and fitting layer, not a runtime dependency.

The existing `tools/analysis/crt_flicker_analysis.R` should be extended to ingest:

- capture directories
- per-frame CSV metrics from Python
- calibration JSON metadata

Required R outputs:

- temporal luminance stability plots
- step-response fit plots
- LUT residual plots
- drift and flicker summaries
- confidence report for the current calibration set

Recommended new script name if separation is cleaner:

- `tools/analysis/phosphor_calibration_report.R`

The pipeline contract is:

1. Python extracts machine-readable measurements.
2. R fits curves and produces publication-quality diagnostics.
3. The runtime consumes only the final compact LUT artifacts.

## Runtime Limits

Recommended initial limits for the first milestone:

- logical scene: `256x240`
- tile size: `8x8`
- text grid: `32x30`
- palette entries: `256`
- sprite count: `64`
- sprites per line: `16`
- line spans: `96`

These values should be compile-time constants in the runtime and validation thresholds in the host compiler.

## Testing Strategy

### Host tests

Add host-compiled tests for:

- tile decode
- sprite clipping per line
- text glyph row extraction
- transition-cost accounting
- palette LUT application
- calibration artifact parsing

These tests should follow the repository’s existing `assert.h` style.

### Hardware validation

Required on-device validation:

- register PhosphorOS hooks against the current `crt_core`
- verify stable lock on NTSC and PAL
- verify no DMA underrun regression
- verify calibration scene renders deterministically
- verify palette LUT visibly changes step wedge output as expected

### Analysis validation

The calibration pipeline is not done until:

- Python emits reproducible metrics for the same capture set
- R reports stable gamma and residual values
- generated LUTs round-trip into the runtime without manual editing

## Implementation Sequence

1. Add `phosphor_scene` fixed-capacity runtime structs.
2. Add host compiler for tiles, sprites, glyphs, and palette banks.
3. Implement `phosphor_scanline_hook()` with tilemap-only rendering.
4. Add text plane and sprite composition with per-line clipping.
5. Add transition-cost accounting and deterministic degradation.
6. Add calibration scene emission and Python extraction.
7. Add R report generation and LUT artifact export.
8. Add optional `phosphor_mod_hook()` effects and phase-aware palette extensions.

## Result

This design keeps the repository honest about what composite video actually is. The ESP32 remains a deterministic scanline renderer. The host does the expensive compilation and measurement work. The compositor does not pretend the screen is only pixels; it treats each active line as a bounded resonant transaction whose transitions must fit both the analog medium and the runtime budget.
