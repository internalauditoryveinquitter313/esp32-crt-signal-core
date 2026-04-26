# Vacuum-Neuromorphic Ballistic Computing Direction

This project exists to make CRT-era analog signal hardware usable as a
controlled experimental substrate for physical reservoir computing, glitch
dynamics, and hardware-aware neuromorphic research.

The working theory is the Teoria da Computacao Balistica de
Vacuo-Neuromorfica (TCBVN): computation can be explored across systems where
electron transport, analog memory, and stochastic non-idealities are not hidden
implementation details, but first-class computational resources.

This repository does not claim to implement a vacuum neuromorphic accelerator.
It implements the deterministic signal core needed to drive and instrument one
practical part of that research stack: CRT composite video generation on ESP32.

## Research Thesis

TCBVN combines four research threads:

- Vacuum microelectronics: NVCTs and VFETs use nanoscale vacuum channels,
  field emission, and ballistic electron transport as device-level primitives.
- Physical reservoir computing: nonlinear physical systems can map temporal
  inputs into richer state spaces when they have fading memory, measurable
  state, and controllable perturbation.
- Analog in-memory computing: PCM/ReRAM crossbars perform matrix-vector
  operations in-place through conductance, Ohm's law, and Kirchhoff's laws,
  while device noise and drift become model parameters.
- Network bending and latent intervention: model behavior can be studied by
  perturbing internal activations, representations, or hardware execution
  conditions during inference.

The CRT path matters because a cathode-ray tube is a vacuum electron-beam
system with magnetic/electrostatic deflection, phosphor persistence, analog
bandwidth limits, and measurable nonlinear artifacts. It is not merely a
display target. For this project, it is the first available physical reservoir
candidate.

## Grounded Claims

- NVCT literature supports nanoscale vacuum channels, field emission,
  Fowler-Nordheim behavior, and ballistic transport as real device mechanisms.
- AIHWKIT and related AIMC work support hardware-aware simulation of analog
  crossbars, including noise, drift, update asymmetry, device-to-device
  variation, DAC/ADC limits, and Tiki-Taka-style training.
- Physical reservoir computing literature supports using nonlinear dynamical
  substrates beyond conventional neural networks, provided the substrate has
  measurable state richness, nonlinearity, memory, and a trainable readout.
- CRT deflection, composite modulation, phosphor response, capture artifacts,
  and yoke/audio-drive experiments are plausible reservoir inputs and outputs,
  but require measurement before being treated as useful computation.

## Speculative Claims

These claims remain hypotheses until this repository can measure them:

- A CRT driven by deterministic composite video can expose enough nonlinear
  state richness to function as a useful reservoir for time-series tasks.
- Controlled glitch, PAL/NTSC phase behavior, phosphor decay, and capture-card
  artifacts can improve separability rather than merely add noise.
- External analog systems such as yoke modulation, memristive devices,
  photonic/acousto-optic devices, or stressed ASIC timing sources can be coupled
  into the same instrumentation loop.
- Network bending signals can be encoded into video/deflection waveforms and
  read back as physical perturbations with measurable model-side utility.

## Role of esp32-crt-signal-core

The firmware must remain signal-first. It should provide repeatable physical
stimuli before it tries to provide visually rich demos.

The current architecture maps directly to the research loop:

- `crt_timing`: establishes deterministic NTSC/PAL timing, line classes, and
  phase behavior.
- `crt_core`: owns scanline synthesis, DMA slot preparation, and realtime
  scheduling.
- `crt_hal`: owns I2S0, DAC0, GPIO25, DMA, ISR, and APLL clock selection.
- `crt_waveform` and `crt_line_policy`: define sync, blanking, burst, and
  per-line policies as inspectable physical primitives.
- `crt_fb`, `crt_compose`, and `crt_tile`: encode stimuli into active video
  without making the framebuffer the architecture center.
- `crt_stimulus`: emits deterministic measurement patterns through the
  compositor, including ramps, checkerboards, PRBS, impulse, chirp, and
  frame-marker bands.
- `crt_diag`: provides the beginning of a measurement plane for late lines,
  queue depth, and ISR behavior.

## Immediate Engineering Direction

1. Preserve deterministic composite output as the invariant.
2. Add stimulus modes that are useful for measurement:
   - RGB332 color ramps;
   - impulse and chirp patterns;
   - checkerboards and phase-sensitive patterns;
   - pseudo-random binary sequences;
   - scanline-coded metadata bands for capture alignment.
3. Extend telemetry so experiments can correlate firmware timing with captured
   analog output.
4. Build a capture pipeline that records CRT/capture-card response and extracts
   reservoir state vectors.
5. Add offline notebooks or host tools that train simple readouts on captured
   state vectors before claiming any reservoir-computing result.

## Boundaries

- Do not sacrifice sync stability for richer visuals.
- Do not put blocking I/O, allocation, or logging in hot paths.
- Do not claim neuromorphic acceleration from CRT output alone.
- Do not treat ESP_8_BIT_composite PAL behavior as authoritative; it is useful
  for RGB332 DAC tables and APLL coefficients, while PAL timing remains owned
  by this project.
- Do not collapse the project into a framebuffer demo. The framebuffer is an
  adapter; the signal is the experiment.

## Reference Anchors

- Structure Optimization of Planar Nanoscale Vacuum Channel Transistor:
  https://www.mdpi.com/2072-666X/14/2/488
- IBM Analog Hardware Acceleration Kit:
  https://github.com/IBM/aihwkit
- Hardware-aware training for large-scale AI inference:
  https://pmc.ncbi.nlm.nih.gov/articles/PMC10441807/
- Toward Thermodynamic Reservoir Computing:
  https://arxiv.org/abs/2601.01916
- ESP_8_BIT_composite reference implementation:
  https://github.com/Roger-random/ESP_8_BIT_composite
