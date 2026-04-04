# PhosphorOS

## A Hybrid Direction for CRT Compose

**PhosphorOS** is the working name for the next major layer above the current ESP32 CRT signal engine: a usable 8-bit compositor library with a serious research program around feedback, optics, and computation through the display itself.

This is explicitly a **hybrid** project.

- Half of the effort goes toward a practical library that people can use to compose tiles, sprites, layers, text, masks, and signal-native effects on a real CRT.
- Half of the effort goes toward turning the CRT chain into an experimental system for camera feedback, closed-loop calibration, and optical computing.

The important decision is that these are not two separate projects glued together later. They should share the same signal substrate, the same scene model, the same telemetry, and the same experimental hooks. The compositor gives the research work a stable operational base. The research work gives the compositor a unique identity and a long horizon.

## Why This Exists

The current repository already establishes the right foundation:

- deterministic scanline synthesis
- signal-first architecture
- explicit timing profiles
- bounded stage execution
- DMA-fed line pipeline
- a clean split between hardware, timing, core, diagnostics, and demo rendering

What it does not yet provide is a real composition model. It can generate excellent test signals and simple patterns, but it does not yet define the abstraction layer where authored content, procedural structure, feedback systems, and research instrumentation can all coexist.

**PhosphorOS** is that layer.

The goal is not to recreate a conventional framebuffer graphics library with a CRT output backend. The goal is to create a **signal-native composition environment** that happens to support familiar 8-bit authoring models while remaining open to feedback loops, phase manipulations, and machine-guided experimentation.

## Core Thesis

The strongest version of this project is:

1. a deterministic composite-video engine at the bottom
2. a compositing and scene system in the middle
3. a feedback and experiment layer above it
4. local AI tooling around the edges, never in the hard realtime path

That yields a stack with two simultaneous identities:

- **usable library**: make real scenes, demos, games, installations, and signal art
- **research instrument**: treat CRT output plus camera capture as a measurable dynamical system

If the project leans only toward usability, it risks becoming another charming retro graphics layer. If it leans only toward research, it risks becoming an unstable lab notebook with no durable interface. The hybrid path is the right one.

## Product Definition

PhosphorOS should be understood as a library family rather than a single monolith.

At minimum, it should offer:

- an 8-bit composition API
- a scene graph or layer stack tuned for scanline-era constraints
- signal-aware effects that operate below the level of ordinary pixels
- camera-driven measurement and calibration tools
- a research harness for optical feedback experiments
- local Ollama integration for authoring, analysis, and experiment control

The user should be able to do all of the following within one conceptual system:

- draw a sprite scene with palettes and tile maps
- add scanline wobble, phase modulation, burst gating, and luma masks
- point a camera at the CRT and measure alignment, brightness, ringing, and geometry drift
- run controlled feedback experiments across frames
- ask a local model to generate parameter schedules, scene variations, or experiment proposals

## Relationship to the Existing Repository

The current repository already contains the signal substrate. PhosphorOS should not replace that work. It should formalize a higher layer on top of it.

The existing base remains:

- `crt_hal`: hardware ownership, DMA, DAC, I2S, ISR
- `crt_timing`: NTSC/PAL timing and line classification
- `crt_core`: lifecycle, prep loop, stage execution, scanline ABI
- `crt_diag`: telemetry
- `crt_demo`: current pattern generation
- `crt_fb`: optional framebuffer adapter stub

PhosphorOS should sit above this and gradually absorb the role currently held by `crt_demo`, while leaving the low-level engine disciplined and small.

A clean framing is:

- **Signal Core**: the machine that guarantees video timing
- **PhosphorOS**: the machine that decides what the signal means

## Architectural Principles

### 1. Signal-first, not framebuffer-first

The physical truth of the system is a timed analog waveform, not an abstract 2D image. The API can present pixels, tiles, and sprites, but internally the system should remain honest about scanlines, active windows, subcarrier phase, porch regions, and modulation opportunities.

### 2. Realtime purity at the bottom

No dynamic allocation after start. No blocking in the hot path. No model inference in the prep loop. No camera processing inside signal generation. No host-like abstractions leaking into the bounded stages.

### 3. Composition as a constrained language

The compositor should not try to emulate a modern retained-mode desktop stack. It should be small, intentional, and expressive within clear limits:

- indexed color
- palette banks
- tiles
- sprites
- text cells
- line effects
- masks
- programmable scene passes

### 4. Research hooks are first-class

Feedback, measurement, and optical experiments are not debugging extras. They are part of the architecture. This means stable capture formats, reproducible experiment configuration, saved telemetry, and explicit ownership of calibration data.

### 5. Determinism first, intelligence second

Ollama is useful, but only as an assistant to authoring and experiment selection. The model never owns the timing-critical path. It proposes. The deterministic engine executes.

## Identity of the Library

PhosphorOS should feel like an operating system for phosphor behavior, not just a graphics package.

That name earns its keep if the project treats the screen as:

- a display surface
- a measurement surface
- a feedback medium
- a weak analog computer

That is the conceptual center.

## Core Modules

The module split should be explicit early, even if some modules start as thin stubs.

### `phosphor_compose`

The main composition engine.

Responsibilities:

- compose layers into the active scanline region
- manage scene ordering and blending rules
- expose a stable render API for tilemaps, sprites, text, and procedural layers
- support both whole-frame state and per-scanline callbacks
- translate high-level composition intent into bounded active-window rendering

This is the practical heart of the library.

### `phosphor_scene`

The authored content model.

Responsibilities:

- scene description
- layer stack
- sprite lists
- tile maps
- palette banks
- text/grid primitives
- masks and clip regions
- temporal scheduling of scene changes across frames

This module should define the “what to draw” side without knowing about DMA or DAC details.

### `phosphor_assets`

A compact asset layer for retro-native content.

Responsibilities:

- indexed sprite storage
- tile and charset storage
- palette definitions
- compressed pattern banks
- optional host-side conversion tools

The asset format should be biased toward deterministic runtime decode and very small memory footprints.

### `phosphor_signal_fx`

Effects that are aware of the signal as a waveform, not just an image.

Responsibilities:

- luma-only perturbations
- chroma phase offsets
- burst modulation
- horizontal shimmer
- wobble and bend
- line-specific gating
- subcarrier-synchronous patterning
- programmable porch and blanking experiments where safe

This is where the project becomes distinctly CRT-native.

### `phosphor_math`

Shared mathematical machinery.

Responsibilities:

- fixed-point phase arithmetic
- coordinate transforms between logical space and scanline space
- small matrix/vector routines for calibration
- convolution kernels for measurement and analysis
- curve fitting for camera alignment and timing error
- feedback-controller helpers

This module prevents the system from devolving into ad hoc equations spread across the repo.

### `phosphor_feedback`

Closed-loop control and experiment orchestration.

Responsibilities:

- parameter schedules
- feedback policies
- experiment state machines
- frame-to-frame adaptation
- controlled perturbation injection
- logging of cause and observed effect

This module is where the library stops being only a renderer and starts becoming an instrument.

### `phosphor_camera`

The camera interface and capture analysis layer.

Responsibilities:

- capture session metadata
- frame alignment
- crop and geometry calibration
- intensity normalization
- phosphor bloom and persistence estimation
- artifact scoring
- camera-to-CRT coordinate registration

This should be usable both on recorded footage and on live camera input from a host-side tool.

### `phosphor_optics`

The explicit research module for optical computing.

Responsibilities:

- define experiments where the display and camera pair form part of the compute path
- host iterative image-field transforms
- measure attractors, stability regions, and memory effects
- support feedback kernels and spatiotemporal recirculation

This is not a gimmick module. It is the research nucleus.

### `phosphor_ollama`

The local model integration boundary.

Responsibilities:

- prompt templates for scene authoring
- structured experiment generation
- telemetry summarization
- parameter search assistance
- host-side conversation with the system through a schema, not loose strings

This module should treat Ollama as a local copilot, critic, and search engine for the experiment space.

### `phosphor_lab`

A reproducibility and tooling layer.

Responsibilities:

- save experiment manifests
- store calibration sets
- attach camera captures to run metadata
- replay parameter schedules
- compare runs over time
- export reports and plots

Without this, the research side becomes impossible to trust.

## Composition Model

The composition model should stay deliberately 8-bit even when the signal underneath is richer.

Recommended primitives:

- background tilemap
- foreground tilemap
- sprite list
- text plane
- line attribute table
- palette banks
- stencil or mask layer
- procedural layer hook
- post-compose signal-fx pass

The key design question is where to stop pretending in terms of “pixel purity.” A strong answer is:

- authoring is mostly pixel-like
- runtime execution is scanline- and phase-aware

That gives users a friendly mental model while preserving the project’s core originality.

## Rendering Model

PhosphorOS should support at least three rendering paths:

### 1. Compose-to-active-window

The default path.

- The library renders only the active video region.
- Sync, porch, and burst remain owned by core signal stages.
- The compositor produces indexed or luma/chroma-directed samples for active content.

### 2. Signal-native modulation

An advanced path.

- Content is composed normally.
- A second pass modifies the full line or active region using phase- and waveform-aware transforms.
- This is how the system expresses effects that are impossible in a plain framebuffer model.

### 3. Feedback-directed rendering

A research path.

- Camera-derived metrics alter scene, palette, timing, or effect parameters over time.
- The feedback loop is bounded and rate-limited.
- The hot path only consumes already-computed parameters.

## Math Foundations

If this library is going to hold together, its math needs to be explicit from the start.

### Coordinate Systems

The system has at least six important spaces:

1. **logical scene space**
   Author-facing tile, sprite, and text coordinates.

2. **active video sample space**
   Horizontal sample indices within the active region of a scanline.

3. **physical scanline space**
   Full line coordinates including sync, porch, burst, and active segments.

4. **subcarrier phase space**
   Q20 fixed-point phase progression for chroma-sensitive operations.

5. **camera image space**
   Captured raster coordinates after optics and sensor distortion.

6. **optical state space**
   A higher-level space describing the measured screen as a dynamic physical system over time.

The project needs named transforms between these spaces rather than improvised conversions.

### Fixed-Point First

The runtime math should prefer fixed-point in the hot path.

Recommended conventions:

- Q20 for subcarrier phase, already aligned with current scanline ABI
- Q8.8 or Q4.12 for spatial interpolation where needed
- integer-only blend and palette paths for composition
- host-side floating point allowed for calibration, fitting, and offline analysis

This preserves determinism and avoids accidental drift between platforms.

### Signal Equations

A useful working abstraction for one scanline is:

`y[n] = B[n] + S[n] + C[n] + M[n]`

Where:

- `B[n]` is the baseline blanking and porch structure
- `S[n]` is sync content
- `C[n]` is composed active content
- `M[n]` is modulation or experimental perturbation

For feedback experiments across frames:

`x_(t+1) = F(x_t, u_t, o_t)`

Where:

- `x_t` is internal experiment state
- `u_t` is the control vector chosen for frame `t`
- `o_t` is the observed optical measurement after capture and analysis

This framing matters because it keeps the project legible as a control system, not just a renderer with hacks.

### Calibration Math

Camera feedback requires at least:

- affine transforms for crop and alignment
- homography estimation if the camera is not perfectly normal to the display
- per-channel response normalization
- temporal smoothing
- radial distortion correction where necessary
- fitted timing offsets between generated line structure and observed image structure

If this work is done well, the system can build a stable mapping from emitted line/sample coordinates to captured optical coordinates.

### Feedback and Stability

Once the display loop is closed through a camera, naive control becomes unstable quickly. The library should assume:

- latency
- noise
- phosphor persistence
- rolling or global shutter effects
- camera auto-exposure interference
- nonlinear analog response

Therefore feedback policies should support:

- low-pass filtered observation
- bounded update step sizes
- hysteresis
- region-of-interest scoring
- experimental deadbands
- watchdog rollback to known-good parameters

### Field Theory Mindset

The optical-computing side should think in terms of fields, not just images.

Useful quantities include:

- intensity fields
- temporal difference fields
- persistence fields
- phase response fields
- error fields between intended and observed states
- attractor maps over repeated feedback iterations

This helps the project move beyond “camera sees screen” into “camera measures a dynamical medium.”

## Camera Feedback

Camera feedback is not just for screenshots. It is a way to close the loop between emitted signal and observed reality.

### Primary Uses

- auto-calibrate geometry and framing
- estimate black and white levels as seen on the real display
- measure blur, bloom, ringing, and edge response
- quantify line stability and inter-frame drift
- tune burst amplitude and phase experiments
- observe phosphor persistence and decay
- detect emergent patterns in iterative feedback experiments

### Camera Pipeline

A strong first version should look like this:

1. emit a calibration pattern from PhosphorOS
2. capture the CRT with a fixed camera
3. detect corners, guides, or fiducials
4. solve the geometry mapping
5. normalize brightness and crop the screen region
6. compute metrics relevant to the current experiment
7. feed those metrics into a bounded control layer

### Calibration Patterns

The library should provide dedicated patterns for measurement, not just pretty demos.

Examples:

- grid with fiducial corners
- line-pair resolution chart
- step wedges for luminance response
- phase-coded stripe patterns
- persistence test flashes
- geometry warp templates
- chroma/luma separation targets

### Metrics Worth Tracking

- line straightness
- horizontal and vertical scale error
- frame drift
- overshoot and ringing
- halo radius
- temporal decay curve
- contrast transfer estimate
- spatial nonuniformity
- palette response under camera capture

### Closed-Loop Use Cases

- auto-center the image
- compensate for mild geometry warp
- find stable burst amplitude ranges
- tune wobble or shimmer for a desired visible intensity
- seek interesting optical attractors while respecting safety bounds
- search for patterns that are stable on a given CRT-camera pair but not in simulation

This last point is important. The most interesting results may be properties of the physical loop, not of the digital scene alone.

## Optical Computing

This is the bold part, and it should remain explicit rather than buried as “future experiments.”

The CRT plus camera chain can be treated as a weak analog processor with:

- spatial blur
- persistence
- thresholding through capture
- nonlinear brightness response
- geometry distortion
- noise
- temporal coupling across frames

Those are usually seen as defects. Here they can become compute primitives.

### Research Direction

The central research question is:

**What useful or interesting transformations can be computed by iterating patterns through a CRT-camera feedback loop, with the ESP32 controlling the emitted signal and a host system analyzing the observed result?**

That can split into several agendas.

### 1. Reservoir-like visual dynamics

Treat the CRT-camera system as a reservoir with memory.

Possible experiments:

- frame-to-frame recurrence with controlled gain
- delayed feedback with line or frame offsets
- input perturbation and attractor measurement
- pattern classification via response signature

This is not “AI hype.” It is a concrete question about whether the physical system exposes exploitable stateful dynamics.

### 2. Analog iterative image transforms

Use the display-capture loop to perform repeated transforms such as:

- edge enhancement by overshoot exploitation
- blur-threshold iterations
- persistence-weighted accumulation
- morphology-like repeated expansion and decay
- phase-sensitive stripe selection

The goal is not raw efficiency. The goal is to discover transformations that are native to the medium.

### 3. Optical memory and decay

Measure whether phosphor persistence plus camera integration can encode useful short-term state.

Possible outputs:

- decay kernels by phosphor color and brightness
- memory depth estimates
- persistence-based temporal mixing operators

### 4. Computing with alignment error

Slight misregistration between emission and observation may produce useful dynamics.

Potential areas:

- contour drift
- geometric recirculation
- cellular-like evolution under warped remapping
- self-stabilizing motifs

### 5. Human-steerable cybernetic visuals

Not every experiment needs a classical compute objective. Some should optimize for:

- expressiveness
- controllability
- repeatability
- aesthetic richness
- interaction with audio or performer input

PhosphorOS should support both scientific and artistic outcomes.

## Role of Simulation

A host-side simulator will matter, but it must be treated honestly.

Simulation can help with:

- API development
- asset iteration
- composition debugging
- rough tuning of feedback laws
- offline experimentation

Simulation cannot fully substitute for:

- phosphor persistence
- analog blur and ringing
- camera artifacts
- the peculiar dynamics of a real CRT

So the simulator should be positioned as a companion, not as truth.

## Ollama Integration

Ollama should be treated as a **local intelligence layer**, not as a magic runtime.

### Hard Boundary

Ollama does not belong in:

- ISR logic
- scanline prep
- DMA scheduling
- line-stage execution
- anything that affects deterministic timing directly

Ollama does belong in:

- authoring workflows
- structured scene generation
- parameter exploration
- experiment planning
- telemetry summarization
- camera-run interpretation

### Good Uses for Ollama

#### Scene authoring

Turn structured prompts into:

- tilemap sketches
- palette proposals
- text-mode scene layouts
- effect chain presets
- demo scripts

The result must be a validated schema, not arbitrary code generation.

#### Experiment design

Given a goal such as “find stable edge-emphasizing feedback patterns,” Ollama can propose:

- parameter sweeps
- candidate recurrence equations
- capture schedules
- metrics to log
- stop conditions

#### Lab assistant role

Given telemetry, captures, and metadata, Ollama can:

- summarize what changed across runs
- identify candidate anomalies
- suggest follow-up experiments
- compare observed results against expected pattern families

#### Promptable signal composition

Longer term, the system could support a constrained prompt interface:

- “Generate a title card with luma bars and chroma shimmer.”
- “Make a feedback scene that emphasizes persistent diagonals.”
- “Search for a stable low-gain attractor that preserves text legibility.”

That only works if the prompt compiler targets a strict intermediate representation.

### Recommended Integration Shape

Use a host-side service or tool that speaks to Ollama and emits structured artifacts:

- scene JSON or TOML
- experiment manifests
- parameter schedules
- palette definitions
- capture-analysis summaries

PhosphorOS then ingests those artifacts deterministically.

### Models and Workflow

A practical split could be:

- small fast model for summarization and prompt normalization
- larger local model for experiment ideation and scene generation
- optional embeddings for indexing prior experiment logs and captures

The real value is not “AI-generated visuals.” The real value is a local research assistant that helps explore a huge parameter space without surrendering control.

## Developer Experience

PhosphorOS should be pleasant to use in layers.

### Layer 1: embedded-only use

For users who just want a deterministic CRT compositor:

- configure timing
- load assets
- define scene
- register effect passes
- start engine

### Layer 2: host-assisted authoring

For users who want better tools:

- host-side asset conversion
- simulator previews
- camera calibration
- scene compilation
- Ollama-backed scene and experiment assistants

### Layer 3: full lab mode

For research users:

- repeatable capture sessions
- experiment manifests
- comparative metrics
- feedback policies
- optical computing workflows

This layering is important because it keeps the practical library usable even if the research tooling is still evolving.

## Deliverables

The project needs concrete outputs, not just ambition.

### Library Deliverables

1. **PhosphorOS composition core**
   An 8-bit compositor with tiles, sprites, text, palette banks, masks, and scanline-aware render hooks.

2. **Signal FX module**
   A small but distinct set of CRT-native modulation passes that clearly exceed a normal framebuffer renderer.

3. **Asset pipeline**
   Host tools for sprite, palette, tile, and charset conversion into compact runtime formats.

4. **Scene format**
   A stable schema for scenes, presets, and demo scripts.

5. **Simulator**
   A host-side approximate preview path for authoring and debugging.

### Camera and Feedback Deliverables

6. **Calibration suite**
   Reference patterns plus capture-analysis tools for alignment, brightness normalization, and geometry mapping.

7. **Capture metric toolkit**
   Repeatable extraction of blur, ringing, drift, decay, and contrast metrics from camera footage.

8. **Closed-loop controller**
   A bounded system that can modify compositor or signal parameters based on observed camera metrics.

### Research Deliverables

9. **Optical computing harness**
   A formal experiment runner for iterative display-capture loops with saved metadata and replay.

10. **Experiment catalog**
   A set of named experiments with documented hypotheses, parameters, and observed results.

11. **Run archive**
   Structured storage of captures, plots, telemetry, configuration, and conclusions.

### Ollama Deliverables

12. **Structured Ollama bridge**
   A host-side bridge that turns prompts and logs into validated scene manifests and experiment plans.

13. **Prompt packs**
   Curated prompt templates for scene design, calibration analysis, and research exploration.

14. **Run analyst**
   A local AI-assisted report generator for comparing experiment sessions and suggesting next steps.

## Suggested Milestones

### Phase 1: usable compositor

- define scene and asset schemas
- implement tilemap, sprite, text, and palette composition
- replace demo-only rendering with compose-driven rendering
- expose basic signal-fx hooks

Success condition:

The system can render authored scenes and simple effects on real CRT hardware reliably.

### Phase 2: camera truth

- build calibration patterns
- create host-side capture analyzer
- solve camera-to-screen mapping
- quantify real-world image defects and stability

Success condition:

The project can measure what the display is actually doing rather than relying on intuition.

### Phase 3: bounded feedback

- define feedback state and policy abstractions
- implement low-rate closed-loop adaptation
- tune for safe, stable control

Success condition:

The scene or signal can react to measured camera output without destabilizing the core engine.

### Phase 4: optical lab

- define experiment manifest format
- implement iterative display-capture workflows
- build first catalog of optical-computing experiments

Success condition:

The project can run reproducible experiments where the physical display path participates in computation.

### Phase 5: Ollama copilot

- add scene-manifest generation
- add experiment ideation and report generation
- connect run archives to local semantic search

Success condition:

The local model reduces exploration cost without violating deterministic system boundaries.

## Non-Negotiable Constraints

These should remain true throughout the project:

- signal integrity outranks visual richness
- composition must not compromise timing determinism
- the hot path stays allocation-free and bounded
- AI stays outside realtime execution
- camera feedback is rate-limited and supervised
- every research mode can be disabled cleanly
- practical library value must continue even if research features are unfinished

## What Would Make This Special

Many projects can claim “retro graphics on embedded hardware.”

Very few can claim all of the following at once:

- real composite-video signal ownership
- scanline-accurate deterministic composition
- signal-native visual effects
- closed-loop camera calibration
- optical feedback experiments
- local-model-assisted exploration

That combination is the identity. If executed well, PhosphorOS will not read as an ESP32 demo library. It will read as a compact, strange, serious instrument for composing with phosphor.

## Recommended Next Moves

The next planning pass should break this vision into implementable vertical slices.

The first slices should likely be:

1. define the PhosphorOS scene model and composition contract
2. replace `crt_demo` with a minimal compose-driven renderer
3. define the host-side calibration and capture-analysis pipeline
4. specify the experiment manifest and Ollama bridge format

That sequence preserves the hybrid commitment from the beginning: a real library, a real lab, one coherent system.
