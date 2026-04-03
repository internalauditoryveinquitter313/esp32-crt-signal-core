<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&color=0:08090d,50:34131f,100:153222&height=200&section=header&text=CRT%20Signal%20Core&fontSize=55&fontColor=b8ff6a&animation=twinkling&fontAlignY=35&desc=Deterministic%20Composite%20Video%20Engine%20for%20ESP32&descSize=16&descAlignY=55&descColor=e5e7eb" width="100%"/>

<img src="./.github/assets/television.png" alt="Retro CRT television icon" width="72"/>

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-5.4+-C11?style=for-the-badge&logo=espressif&logoColor=fff)](https://docs.espressif.com/projects/esp-idf/)
[![C](https://img.shields.io/badge/C11-Embedded-555?style=for-the-badge&logo=c&logoColor=fff)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![ESP32](https://img.shields.io/badge/ESP32--D0WD--V3-GPIO25-e7352c?style=for-the-badge&logo=espressif&logoColor=fff)](https://www.espressif.com/en/products/socs/esp32)
[![Tests](https://img.shields.io/badge/tests-4_suites-00875A?style=for-the-badge)](./tests)
[![License](https://img.shields.io/badge/license-MIT-2d1b69?style=for-the-badge)](./LICENSE)

**Scanlines are the heartbeat. Sync is the contract. The signal never lies.**

---

*"The phosphor doesn't care about your framebuffer. It cares about the next 63.5µs."*

</div>

---

> [!IMPORTANT]
> **Signal-first architecture.** The scanline is the realtime unit, not the frame.
> Every stage — sync, burst, active video — is a deterministic pipeline stage
> executed on a pinned core with zero allocations after init. If you can't finish
> the line in time, you shed stages. You never lose sync.

---

## ⚡ Quick Start

```c
#include "crt_core.h"

void app_main(void)
{
    crt_core_config_t config = {
        .video_standard       = CRT_VIDEO_STANDARD_NTSC,
        .demo_pattern_mode    = CRT_DEMO_PATTERN_COLOR_BARS_RAMP,
        .target_ready_depth   = 4,
        .min_ready_depth      = 2,
        .prep_task_core       = 1,
    };

    crt_core_init(&config);
    crt_core_start();
    // GPIO25 is now outputting NTSC composite video via DAC
}
```

```bash
# Build & flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## 🏗️ Architecture

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'background': '#0b0d12',
  'primaryTextColor': '#f8fafc',
  'primaryBorderColor': '#c7d2fe',
  'lineColor': '#9be564',
  'secondaryColor': '#160f14',
  'tertiaryColor': '#10161f',
  'clusterBkg': '#10161f',
  'clusterBorder': '#7f1d1d',
  'fontFamily': 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace'
}}}%%
flowchart LR
    subgraph Core1["🔧 Prep Task (Core 1)"]
        direction TB
        BLANK["Blanking<br/>Front/Back Porch"]
        SYNC["Sync Pulse<br/>H-Sync / V-Sync"]
        BURST["Color Burst<br/>3.579545 MHz"]
        ACTIVE["Active Video<br/>Pixel → IRE → DAC"]
    end

    subgraph DMA["📡 DMA Ring"]
        RING["Ring Buffer<br/>Slot Queue"]
    end

    subgraph HW["⚡ Hardware"]
        I2S["I2S0<br/>Continuous TX"]
        DAC["DAC Ch.0<br/>8-bit"]
        GPIO["GPIO25<br/>Composite Out"]
    end

    BLANK --> SYNC --> BURST --> ACTIVE
    ACTIVE --> RING
    RING --> I2S --> DAC --> GPIO

    ISR["EOF ISR<br/>Recycle Slots"] -.-> RING

    classDef stage fill:#16351f,stroke:#b8ff6a,color:#f8fafc,stroke-width:2px;
    classDef dma fill:#1c2230,stroke:#a5b4fc,color:#f8fafc,stroke-width:2px;
    classDef hw fill:#5b1720,stroke:#fb7185,color:#f8fafc,stroke-width:2px;
    classDef note fill:#33252b,stroke:#d4d4d8,color:#f8fafc,stroke-width:2px;

    class BLANK,SYNC,BURST,ACTIVE stage;
    class RING dma;
    class I2S,DAC,GPIO hw;
    class ISR note;
```

---

## 📦 Components

| Component             | Role                               | Key Constraint                   |
|:----------------------|:-----------------------------------|:---------------------------------|
| **`crt_core`**        | Engine — orchestrates the pipeline | No alloc after `start()`         |
| **`crt_hal`**         | I2S0 + DAC hardware abstraction    | GPIO25 only, internal SRAM DMA   |
| **`crt_timing`**      | NTSC/PAL timing profiles           | µs-precise blanking/sync tables  |
| **`crt_waveform`**    | Burst & chroma synthesis           | 3.579545 MHz NTSC colorburst     |
| **`crt_line_policy`** | Per-line type decisions            | VBI, sync, active classification |
| **`crt_demo`**        | Test pattern generator             | Color bars, ramps, grids         |
| **`crt_diag`**        | Runtime telemetry                  | Late line detection, ISR stats   |
| **`crt_fb`**          | Framebuffer stub                   | Future: external pixel source    |

---

## 🔬 Signal Pipeline

Each scanline passes through deterministic stages with a strict contract:

```mermaid
%%{init: {'theme': 'base', 'themeVariables': {
  'background': '#0b0d12',
  'primaryTextColor': '#f8fafc',
  'primaryBorderColor': '#c7d2fe',
  'lineColor': '#9be564',
  'secondaryColor': '#160f14',
  'tertiaryColor': '#10161f',
  'clusterBkg': '#10161f',
  'clusterBorder': '#7f1d1d',
  'fontFamily': 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace'
}}}%%
flowchart LR
    SLOT_IN["Recycled DMA Slot"] --> BLANK_STAGE["1. Blanking<br/>Front / Back Porch"]
    BLANK_STAGE --> SYNC_STAGE["2. Sync Pulse<br/>H-Sync / V-Sync"]
    SYNC_STAGE --> BURST_STAGE["3. Color Burst<br/>Phase Reference"]
    BURST_STAGE --> ACTIVE_STAGE["4. Active Video<br/>Pixels -> IRE -> DAC Words"]
    ACTIVE_STAGE --> SLOT_OUT["Ready DMA Slot"]
    SLOT_OUT --> OUTPUT["I2S0 -> DAC -> GPIO25"]

    RULES["No malloc<br/>No blocking<br/>No logging<br/>No peripheral access"] -.-> BLANK_STAGE
    RULES -.-> SYNC_STAGE
    RULES -.-> BURST_STAGE
    RULES -.-> ACTIVE_STAGE
    SHED["If timing slips:<br/>shed optional stages, preserve sync"] -.-> ACTIVE_STAGE

    classDef queue fill:#1c2230,stroke:#a5b4fc,color:#f8fafc,stroke-width:2px;
    classDef stage fill:#16351f,stroke:#b8ff6a,color:#f8fafc,stroke-width:2px;
    classDef rule fill:#33252b,stroke:#d4d4d8,color:#f8fafc,stroke-width:2px;
    classDef output fill:#5b1720,stroke:#fb7185,color:#f8fafc,stroke-width:2px;

    class SLOT_IN,SLOT_OUT queue;
    class BLANK_STAGE,SYNC_STAGE,BURST_STAGE,ACTIVE_STAGE stage;
    class RULES,SHED rule;
    class OUTPUT output;
```

**Stage rules:**
- No `malloc`, no blocking, no logging, no peripheral access
- Each stage writes directly into a preallocated DMA buffer
- If a stage can't complete in time → shed it, keep sync

---

## 📐 Timing Reference

| Parameter        |              NTSC |                 PAL |
|:-----------------|------------------:|--------------------:|
| **Line period**  |         63.556 µs |           64.000 µs |
| **H-sync**       |            4.7 µs |              4.7 µs |
| **Front porch**  |            1.5 µs |             1.65 µs |
| **Back porch**   |            4.7 µs |              5.7 µs |
| **Color burst**  | 2.5 µs (9 cycles) | 2.25 µs (10 cycles) |
| **Active video** |           52.6 µs |            51.95 µs |
| **Burst freq**   |      3.579545 MHz |      4.43361875 MHz |
| **Total lines**  | 525 (262.5/field) |   625 (312.5/field) |
| **Field rate**   |          59.94 Hz |            50.00 Hz |

---

## 📂 Project Structure

```
esp32-crt-signal-core/
├── main/
│   └── app_main.c                          # Entry point
├── components/
│   ├── crt_core/                           # Engine + pipeline stages
│   │   ├── include/
│   │   │   ├── crt_core.h                  # Public API
│   │   │   ├── crt_stage.h                 # Stage contract
│   │   │   ├── crt_waveform.h              # Burst/chroma synthesis
│   │   │   └── crt_line_policy.h           # Line type classifier
│   │   ├── crt_core.c
│   │   ├── crt_waveform.c
│   │   └── crt_line_policy.c
│   ├── crt_hal/                            # I2S0 + DAC driver
│   ├── crt_timing/                         # NTSC/PAL timing profiles
│   ├── crt_demo/                           # Test pattern generator
│   ├── crt_diag/                           # Runtime telemetry
│   └── crt_fb/                             # Framebuffer interface (stub)
├── tests/                                  # Host-compiled C tests
│   ├── burst_waveform_test.c
│   ├── crt_timing_profile_test.c
│   ├── crt_demo_pattern_test.c
│   └── line_policy_test.c
├── docs/                                   # Reference docs
├── .clang-format                           # Code style (embedded C)
├── .clang-tidy                             # Static analysis config
├── .editorconfig                           # Editor consistency
├── CMakeLists.txt                          # ESP-IDF project root
└── sdkconfig                               # ESP-IDF Kconfig
```

---

## 🛠️ Build

<details>
<summary><strong>📋 Prerequisites</strong></summary>

| Tool       | Version                   |
|:-----------|:--------------------------|
| ESP-IDF    | `>= 5.4`                  |
| CMake      | `>= 3.16`                 |
| GCC (host) | For running tests locally |
| Target SoC | ESP32-D0WD-V3             |

</details>

```bash
# Clone
git clone https://github.com/gabrielmaialva33/esp32-crt-signal-core.git
cd esp32-crt-signal-core

# Source ESP-IDF (if not already)
. $IDF_PATH/export.sh

# Build
idf.py build

# Flash & monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

### Running Tests (Host)

```bash
# Burst waveform synthesis
gcc -I components/crt_core/include -I components/crt_timing/include \
    tests/burst_waveform_test.c components/crt_core/crt_waveform.c \
    -lm -o /tmp/burst_test && /tmp/burst_test

# Line policy
gcc -I components/crt_core/include -I components/crt_timing/include \
    tests/line_policy_test.c components/crt_core/crt_line_policy.c \
    -o /tmp/policy_test && /tmp/policy_test

# Timing profiles
gcc -I components/crt_timing/include \
    tests/crt_timing_profile_test.c components/crt_timing/crt_timing.c \
    -o /tmp/timing_test && /tmp/timing_test

# Demo patterns
gcc -I components/crt_core/include -I components/crt_timing/include \
    -I components/crt_demo/include \
    tests/crt_demo_pattern_test.c components/crt_demo/crt_demo_pattern.c \
    components/crt_core/crt_waveform.c -lm \
    -o /tmp/demo_test && /tmp/demo_test
```

---

## 📊 Stats

| Metric             |           Value |
|:-------------------|----------------:|
| **C source files** |               8 |
| **Header files**   |               8 |
| **Test suites**    |               4 |
| **Total lines**    |           1,676 |
| **Components**     |               6 |
| **DMA channels**   | I2S0 continuous |
| **DAC resolution** |           8-bit |
| **Output pin**     |          GPIO25 |

---

## 📜 License

[MIT](./LICENSE) — Gabriel Maia

---

<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&color=0:0a0a0a,50:1a1a2e,100:2d1b69&height=120&section=footer&fontSize=30&fontColor=00ff41&animation=twinkling" width="100%"/>

*Built with phosphor persistence and scanline discipline.*

<img src="https://img.shields.io/badge/made%20by-Maia-15c3d6?style=flat&logo=appveyor" alt="Maia" >

</div>
