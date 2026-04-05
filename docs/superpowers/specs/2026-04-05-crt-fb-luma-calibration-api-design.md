# CRT FB Luma Calibration API Design

**Date:** 2026-04-05
**Status:** Approved for planning
**Scope:** Firmware-first calibration layer for the indexed-8 framebuffer path, with host-side measurement and appearance-spec extraction driven by webcam capture.

## Goal

Turn the existing `crt_fb` + scanline hook path into a formal calibration and characterization pipeline for luma-first experiments.

Phase 1 prioritizes:

- repeatable `black floor` measurement
- near-black separability
- relative tone-curve capture
- white clipping onset
- temporal stability and perceptual response descriptors

The immediate target is better, more measurable compositor behavior under webcam capture. The longer-term target is a machine-readable spec that can later drive image-editor or model-assisted workflows.

## Non-Goals

Phase 1 does not attempt to provide:

- absolute luminance measurement in physical units
- spectral characterization of phosphors
- closed-loop automatic control inside the realtime scanline path
- a physically complete model of electron-beam or phosphor dynamics

## Constraints

The design must preserve the existing realtime rules:

- no dynamic allocation after `crt_core_start()`
- no blocking, logging, or analysis inside the hot path
- no camera processing in firmware scanline generation
- no reduction in sync integrity to support calibration features

The Logitech C270 is the current operational capture device. For calibration purposes, the relevant operational baseline is:

- webcam present as `/dev/video0`
- `MJPG`, `1280x720`, `30 fps`
- manual exposure enabled
- `power_line_frequency = 60 Hz` as the closest operational setting to NTSC `59.94 Hz`

The current observed webcam state and available controls are environment-specific and must be treated as capture context, not hardcoded physical truth.

## Problem Statement

The current framebuffer path can render indexed-8 images and feed the scanline hook efficiently, but it does not provide a formal way to:

- emit named calibration scenes
- declare regions of interest for measurement
- serialize what the firmware intended to display
- compare firmware changes against a stable measurement contract
- extract temporal/perceptual descriptors for future CRT-aware editing or synthesis workflows

As a result, calibration is currently ad hoc and difficult to reproduce.

## Design Overview

The design introduces a calibration layer above `crt_fb` and below host-side analysis.

Responsibilities are split cleanly:

- `crt_core`: deterministic scanout only
- `crt_fb`: indexed-8 composition surface and scanline hook
- `crt_cal` or `crt_fb_cal`: formal calibration-scene API for firmware
- host analysis tools: webcam capture analysis, metric extraction, appearance profiling

This preserves the core engine as a deterministic emitter while making the emitted scene explicit and machine-readable.

## Architectural Model

The phase-1 data flow is:

1. firmware constructs a calibration scene
2. firmware renders that scene into `crt_fb`
3. firmware publishes scene metadata
4. CRT emits the scene through the existing signal core
5. webcam captures the resulting image stream
6. host tools compute measurement metrics and appearance descriptors
7. outputs are stored as structured specs for comparison and future downstream use

This creates two formal outputs:

- `measurement spec`: truth-oriented, quantitative, repeatable
- `appearance spec`: observational and perceptual, with temporal descriptors

## Phase-1 Calibration Scenes

The initial scene set is luma-first and intentionally simple.

### Required scenes

- `flat_black`
- `near_black_steps`
- `flat_mid_gray`
- `flat_white`
- `grayscale_steps`
- `pluge_luma`

### Design rules for scenes

- large, easy-to-measure patches or bands
- low spatial frequency first
- deterministic geometry
- stable palette-index usage
- explicit, named measurement regions

High-frequency diagnostic patterns are intentionally deferred. The C270 is not a trustworthy primary instrument for photometric truth at fine spatial detail.

## Public Firmware API

The new API should be small, explicit, and deterministic.

### Core types

`crt_cal_pattern_kind_t`

- semantic pattern identifiers
- examples: `CRT_CAL_PATTERN_FLAT_BLACK`, `CRT_CAL_PATTERN_NEAR_BLACK_STEPS`, `CRT_CAL_PATTERN_GRAYSCALE_STEPS`

`crt_cal_scene_t`

- a complete scene description
- includes pattern kind, logical geometry, relevant palette indices, and per-pattern parameters

`crt_cal_roi_t`

- named measurement region
- includes logical bounds and semantic purpose

`crt_cal_metadata_t`

- schema version
- scene description
- ROI table
- capture hints
- future-proof extension fields

### Core functions

- scene initializers for each supported pattern
- a renderer that rasterizes a scene into `crt_fb_surface_t`
- a metadata accessor that serializes the intended scene contract
- an optional status/reporting path for the currently active scene id

### API principles

- scene render must be pure with respect to inputs
- same scene input always yields the same framebuffer result
- no hidden dependence on time or capture state
- friendly to host-side unit tests

## Measurement Spec

The `measurement spec` is the quantitative output of a calibration run. It must be designed for regression and cross-run comparison.

### Required measurement families

- `black_floor`
- `near_black_separability`
- `tone_monotonicity`
- `midtone_curve`
- `white_clipping_onset`
- `temporal_stability`
- `spatial_uniformity`

### Mathematical posture

Phase 1 uses relative, empirical metrics:

- robust patch means and medians
- percentiles
- monotonic step comparisons
- simple relative response curves
- temporal mean, variance, and drift summaries

Phase 1 explicitly avoids claiming absolute luminance truth from the webcam alone.

## Appearance Spec

The `appearance spec` is intended for future CRT-aware rendering, editing, and model guidance. It is not the primary calibration truth, but it is a first-class output.

### Required descriptor families

- `luma_response`
- `spatial_response`
- `temporal_response`
- `artifact_profile`
- `capture_context`

### Example appearance attributes

- glow
- bloom
- softness
- halo width
- smear
- bright decay
- dark recovery
- persistence signature
- flicker character

These descriptors should be represented as structured fields, not prose-only notes.

## Host Analysis Role

All heavy analysis stays off-device.

The host pipeline is responsible for:

- aligning capture protocol with the active scene
- reading scene metadata
- extracting measurement metrics from ROIs
- computing temporal descriptors across captured frames
- emitting versioned JSON or equivalent structured outputs

Filters such as CLAHE, temporal averaging, temporal variance maps, band-pass detail views, or Retina-style decompositions are diagnostic tools. They belong to the `inspection path`, not the primary metric path.

## Scientific Limits

Phase 1 is designed to support defensible relative measurement, not full photometric metrology.

The system may support claims like:

- this firmware change improved near-black separation under fixed capture conditions
- this palette tuning reduced white clipping onset
- this configuration has a longer observed bright-decay tail

The system must not overclaim:

- absolute display luminance in physical units
- exact phosphor chemistry or spectral truth
- physically complete tube dynamics inferred from the webcam alone

Temporal and perceptual outputs are observational signatures of the CRT-plus-camera system.

## Testing Strategy

### Firmware tests

Host tests should validate:

- scene geometry
- palette-index assignment
- ROI definitions
- metadata schema population
- deterministic rendering for each scene

### Host validation protocol

Each scene should be captured through a fixed protocol:

- fixed webcam mode
- repeated captures per scene
- comparison across runs
- explicit scene id and capture-context recording

### Success criteria for phase 1

- stable `black floor` extraction across repeated runs
- measurable separation among near-black steps
- monotonic relative grayscale response
- explicit clipping-onset detection
- structured appearance spec output stable enough for regression use

## Implementation Notes

Recommended initial file shape:

- new calibration component or focused module near `crt_fb`
- no changes to the timing-critical scanline ABI beyond metadata wiring where needed
- host-analysis updates should consume metadata instead of reverse-engineering scene layout from images

The indexed-8 framebuffer remains the foundation. The new calibration layer makes that path scientifically useful and future-compatible.

## Future Extensions

After luma-first calibration is working, future phases may add:

- higher-frequency resolution scenes
- chroma-aware calibration scenes
- camera-to-screen registration
- persistence estimation from controlled temporal sequences
- feedback-guided compositor tuning
- downstream export of appearance specs for external image-editor or model workflows

## Decision Summary

Approved design decisions:

- firmware-first calibration API on top of `crt_fb`
- phase 1 prioritizes luma and `black floor`
- structured `measurement spec` and `appearance spec`
- webcam used as a relative observational instrument
- host-side analysis remains outside the realtime signal path
