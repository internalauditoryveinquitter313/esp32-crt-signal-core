# CRT FB Luma Calibration API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a firmware-first luma calibration API on top of `crt_fb`, with named scenes, ROI metadata, and host-analysis outputs that can produce both `measurement spec` and `appearance spec`.

**Architecture:** Keep `crt_core` untouched as the deterministic scanout engine. Implement a focused calibration layer adjacent to `crt_fb` that renders named indexed-8 scenes and exposes machine-readable metadata. Extend host analysis to consume metadata instead of reverse-engineering scene layout from the captured image.

**Tech Stack:** C11, ESP-IDF 5.4, host `gcc` tests with `assert.h`, Python 3 with OpenCV/Numpy for analysis, V4L2 tooling for webcam capture.

---

### Task 1: Make Specs And Plans First-Class Repo Artifacts

**Files:**
- Modify: `.gitignore`
- Test: `git status --short docs/superpowers/specs docs/superpowers/plans`

- [ ] **Step 1: Verify the repo currently ignores new `docs/superpowers` files**

Run: `git check-ignore -v docs/superpowers/specs/2026-04-05-crt-fb-luma-calibration-api-design.md`
Expected: output shows `.gitignore` rule matching `docs/superpowers`

- [ ] **Step 2: Replace the blanket ignore with selective ignores**

```gitignore
/docs/superpowers/*
!/docs/superpowers/specs/
!/docs/superpowers/specs/**
!/docs/superpowers/plans/
!/docs/superpowers/plans/**
/docs/superpowers/tmp/
```

- [ ] **Step 3: Verify new specs and plans are trackable**

Run: `git status --short docs/superpowers/specs docs/superpowers/plans`
Expected: the design spec and plan files appear as addable/tracked candidates

- [ ] **Step 4: Commit the docs versioning fix**

```bash
git add .gitignore docs/superpowers/specs/2026-04-05-crt-fb-luma-calibration-api-design.md docs/superpowers/plans/2026-04-05-crt-fb-luma-calibration-api.md
git commit -m "docs: track calibration specs and plans"
```

### Task 2: Add The Public Calibration API Contract

**Files:**
- Create: `components/crt_fb/include/crt_fb_cal.h`
- Modify: `components/crt_fb/CMakeLists.txt`
- Test: `tests/crt_fb_cal_test.c`

- [ ] **Step 1: Write the failing API contract test**

```c
#include <assert.h>
#include "crt_fb_cal.h"

static void test_scene_init_black_sets_expected_kind(void) {
    crt_fb_cal_scene_t scene;
    assert(crt_fb_cal_scene_init_flat_black(&scene, 256, 240, 0) == ESP_OK);
    assert(scene.kind == CRT_FB_CAL_PATTERN_FLAT_BLACK);
}
```

- [ ] **Step 2: Run the new test to confirm the header/API do not exist yet**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_cal_test.c -o /tmp/crt_fb_cal_test`
Expected: compile failure for missing `crt_fb_cal.h` or unresolved symbols

- [ ] **Step 3: Add the public API header with types and prototypes**

```c
typedef enum {
    CRT_FB_CAL_PATTERN_FLAT_BLACK = 0,
    CRT_FB_CAL_PATTERN_NEAR_BLACK_STEPS,
    CRT_FB_CAL_PATTERN_FLAT_MID_GRAY,
    CRT_FB_CAL_PATTERN_FLAT_WHITE,
    CRT_FB_CAL_PATTERN_GRAYSCALE_STEPS,
    CRT_FB_CAL_PATTERN_PLUGE_LUMA,
} crt_fb_cal_pattern_kind_t;

typedef struct {
    uint16_t x, y, width, height;
    const char *name;
} crt_fb_cal_roi_t;

typedef struct {
    crt_fb_cal_pattern_kind_t kind;
    uint16_t logical_width;
    uint16_t logical_height;
    uint8_t palette_min;
    uint8_t palette_max;
} crt_fb_cal_scene_t;
```

- [ ] **Step 4: Add the new source file to the component build**

```cmake
idf_component_register(SRCS "crt_fb.c" "crt_fb_cal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES crt_core crt_timing)
```

- [ ] **Step 5: Run the test again to confirm it now compiles but still fails on unimplemented behavior**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_cal_test.c components/crt_fb/crt_fb_cal.c -o /tmp/crt_fb_cal_test`
Expected: link or assertion failure until implementation is added

- [ ] **Step 6: Commit the API contract scaffold**

```bash
git add components/crt_fb/include/crt_fb_cal.h components/crt_fb/CMakeLists.txt tests/crt_fb_cal_test.c components/crt_fb/crt_fb_cal.c
git commit -m "feat: add framebuffer calibration API contract"
```

### Task 3: Implement Scene Initializers And ROI Metadata

**Files:**
- Modify: `components/crt_fb/crt_fb_cal.c`
- Modify: `components/crt_fb/include/crt_fb_cal.h`
- Modify: `tests/crt_fb_cal_test.c`

- [ ] **Step 1: Extend the failing test with ROI and metadata expectations**

```c
static void test_pluge_scene_reports_named_rois(void) {
    crt_fb_cal_scene_t scene;
    crt_fb_cal_metadata_t meta;
    assert(crt_fb_cal_scene_init_pluge_luma(&scene, 256, 240, 0, 8, 16) == ESP_OK);
    assert(crt_fb_cal_get_metadata(&scene, &meta) == ESP_OK);
    assert(meta.roi_count >= 3);
    assert(strcmp(meta.rois[0].name, "below_black") == 0);
}
```

- [ ] **Step 2: Run the test and confirm metadata access fails**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_cal_test.c components/crt_fb/crt_fb_cal.c -o /tmp/crt_fb_cal_test && /tmp/crt_fb_cal_test`
Expected: failing assertions for missing metadata behavior

- [ ] **Step 3: Implement scene initializers and metadata access**

```c
esp_err_t crt_fb_cal_get_metadata(const crt_fb_cal_scene_t *scene,
                                  crt_fb_cal_metadata_t *out_meta)
{
    out_meta->schema_version = 1;
    out_meta->scene = *scene;
    /* populate static ROI table based on scene kind */
    return ESP_OK;
}
```

- [ ] **Step 4: Re-run the test until scene initialization and metadata pass**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_cal_test.c components/crt_fb/crt_fb_cal.c -o /tmp/crt_fb_cal_test && /tmp/crt_fb_cal_test`
Expected: `ALL PASSED`

- [ ] **Step 5: Commit the metadata layer**

```bash
git add components/crt_fb/include/crt_fb_cal.h components/crt_fb/crt_fb_cal.c tests/crt_fb_cal_test.c
git commit -m "feat: add calibration scene metadata"
```

### Task 4: Implement Deterministic Rendering For Luma Scenes

**Files:**
- Modify: `components/crt_fb/crt_fb_cal.c`
- Modify: `tests/crt_fb_cal_test.c`
- Test: `tests/crt_fb_cal_test.c`

- [ ] **Step 1: Add failing render tests for each scene family**

```c
static void test_flat_black_fills_entire_surface(void) {
    crt_fb_surface_t fb;
    crt_fb_cal_scene_t scene;
    crt_fb_surface_init(&fb, 16, 8, CRT_FB_FORMAT_INDEXED8);
    crt_fb_surface_alloc(&fb);
    crt_fb_cal_scene_init_flat_black(&scene, 16, 8, 3);
    assert(crt_fb_cal_render_scene(&fb, &scene) == ESP_OK);
    assert(crt_fb_get(&fb, 0, 0) == 3);
    assert(crt_fb_get(&fb, 15, 7) == 3);
}
```

- [ ] **Step 2: Run the scene-render test and confirm rendering is incomplete**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_cal_test.c components/crt_fb/crt_fb.c components/crt_fb/crt_fb_cal.c -o /tmp/crt_fb_cal_test && /tmp/crt_fb_cal_test`
Expected: failing assertions for scene contents

- [ ] **Step 3: Implement pure renderers for the phase-1 patterns**

```c
esp_err_t crt_fb_cal_render_scene(crt_fb_surface_t *surface,
                                  const crt_fb_cal_scene_t *scene)
{
    switch (scene->kind) {
    case CRT_FB_CAL_PATTERN_FLAT_BLACK:
        crt_fb_clear(surface, scene->palette_min);
        return ESP_OK;
    /* add near-black steps, mid gray, white, grayscale steps, PLUGE */
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}
```

- [ ] **Step 4: Verify deterministic rendering**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_cal_test.c components/crt_fb/crt_fb.c components/crt_fb/crt_fb_cal.c -o /tmp/crt_fb_cal_test && /tmp/crt_fb_cal_test`
Expected: `ALL PASSED`

- [ ] **Step 5: Commit the renderers**

```bash
git add components/crt_fb/crt_fb_cal.c tests/crt_fb_cal_test.c
git commit -m "feat: render luma calibration scenes"
```

### Task 5: Integrate A Calibration Mode Into The Demo Firmware

**Files:**
- Modify: `main/app_main.c`
- Modify: `main/CMakeLists.txt`
- Modify: `components/crt_core/Kconfig`
- Test: `bash -c '. ~/esp/esp-idf/export.sh && idf.py build'`

- [ ] **Step 1: Add a compile-time failing test expectation in code comments or static assertions**

```c
/* Calibration mode must compile without the Godzilla image path. */
```

- [ ] **Step 2: Add Kconfig knobs for selecting the startup scene**

```kconfig
config CRT_CALIBRATION_MODE
    bool "Enable framebuffer calibration scenes"

config CRT_CALIBRATION_SCENE
    int "Default calibration scene"
    depends on CRT_CALIBRATION_MODE
```

- [ ] **Step 3: Update `app_main` to choose between image demo and calibration scenes**

```c
if (CONFIG_CRT_CALIBRATION_MODE) {
    crt_fb_cal_scene_t scene;
    crt_fb_cal_scene_init_grayscale_steps(&scene, s_fb.width, s_fb.height, 0, 255);
    crt_fb_cal_render_scene(&s_fb, &scene);
} else {
    memcpy(s_fb.buffer, godzilla_pixels, s_fb.buffer_size);
}
```

- [ ] **Step 4: Build the firmware and verify both code paths compile**

Run: `bash -c '. ~/esp/esp-idf/export.sh && idf.py build'`
Expected: successful build

- [ ] **Step 5: Commit the firmware integration**

```bash
git add main/app_main.c main/CMakeLists.txt components/crt_core/Kconfig
git commit -m "feat: add calibration scene startup mode"
```

### Task 6: Emit Machine-Readable Scene Metadata To The Host

**Files:**
- Modify: `main/app_main.c`
- Modify: `tools/crt_monitor/status.c`
- Modify: `tools/crt_monitor/routes.c`
- Modify: `tools/crt_monitor/static/app.js`

- [ ] **Step 1: Define the initial metadata payload shape**

```json
{
  "schema_version": 1,
  "scene_kind": "grayscale_steps",
  "logical_width": 256,
  "logical_height": 240,
  "rois": [{"name": "step_0", "x": 0, "y": 0, "width": 32, "height": 240}]
}
```

- [ ] **Step 2: Add firmware-side emission of the active calibration scene metadata**

Run: instrument `app_main` or status output to print a single-line JSON blob when the active scene changes.
Expected: host-side status parser can read scene id and ROI schema

- [ ] **Step 3: Extend the monitor/status path to surface the current calibration scene**

Run: `cd tools/crt_monitor && make`
Expected: successful build with new status fields available in the UI data model

- [ ] **Step 4: Commit the metadata transport**

```bash
git add main/app_main.c tools/crt_monitor/status.c tools/crt_monitor/routes.c tools/crt_monitor/static/app.js
git commit -m "feat: publish calibration scene metadata"
```

### Task 7: Add Host Analysis Outputs For Measurement Spec And Appearance Spec

**Files:**
- Modify: `tools/analysis/analyze_crt_capture.py`
- Create: `tools/analysis/calibration_spec_schema.json`
- Create: `tools/analysis/extract_calibration_spec.py`
- Test: `python tools/analysis/extract_calibration_spec.py --help`

- [ ] **Step 1: Write a failing host-side smoke test or command expectation**

Run: `python tools/analysis/extract_calibration_spec.py sample.jpg --scene grayscale_steps`
Expected: command does not exist yet

- [ ] **Step 2: Create a structured schema for phase-1 outputs**

```json
{
  "measurement_spec": {
    "black_floor": {},
    "near_black_separability": {},
    "tone_monotonicity": {}
  },
  "appearance_spec": {
    "luma_response": {},
    "temporal_response": {},
    "artifact_profile": {}
  }
}
```

- [ ] **Step 3: Implement a first extractor that consumes scene metadata and a capture**

```python
def build_measurement_spec(image, scene_meta):
    return {
        "black_floor": {...},
        "near_black_separability": {...},
        "tone_monotonicity": {...},
    }
```

- [ ] **Step 4: Reuse `analyze_crt_capture.py` utilities only for inspection outputs**

Run: keep CLAHE/Retinex/false-color style outputs in the diagnostic branch, not the primary metric path.
Expected: metric path remains simple and explicit

- [ ] **Step 5: Verify the extractor runs**

Run: `python tools/analysis/extract_calibration_spec.py --help`
Expected: usage output with scene/metadata arguments

- [ ] **Step 6: Commit the host extractor**

```bash
git add tools/analysis/analyze_crt_capture.py tools/analysis/calibration_spec_schema.json tools/analysis/extract_calibration_spec.py
git commit -m "feat: extract calibration measurement and appearance specs"
```

### Task 8: Lock The Capture Baseline For Calibration Runs

**Files:**
- Modify: `tools/crt_cam_setup.sh`
- Modify: `README.md`
- Test: `bash tools/crt_cam_setup.sh ntsc /dev/video0`

- [ ] **Step 1: Add an explicit calibration profile for luma runs**

```bash
case "$PROFILE" in
  ntsc-luma)
    # manual exposure, manual WB, fixed gain, defined sharpness
    ;;
esac
```

- [ ] **Step 2: Encode the agreed baseline**

- `auto_exposure = manual`
- `power_line_frequency = 60 Hz`
- `white_balance_automatic = 0`
- fixed `white_balance_temperature`
- low, explicit gain

- [ ] **Step 3: Update README calibration instructions**

Run: document the exact capture-profile command and explain that phase-1 metrics are relative, not absolute.
Expected: README gives a reproducible capture baseline

- [ ] **Step 4: Commit the capture-baseline changes**

```bash
git add tools/crt_cam_setup.sh README.md
git commit -m "docs: add luma calibration capture baseline"
```

### Task 9: Verification Sweep

**Files:**
- Test: `tests/crt_fb_test.c`
- Test: `tests/crt_fb_cal_test.c`
- Test: `tests/crt_scanline_header_test.c`
- Test: `tests/crt_scanline_abi_test.c`
- Test: `tests/crt_timing_profile_test.c`
- Test: firmware build

- [ ] **Step 1: Run framebuffer and calibration tests**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_test.c components/crt_fb/crt_fb.c -o /tmp/crt_fb_test && /tmp/crt_fb_test`
Expected: `ALL PASSED`

- [ ] **Step 2: Run calibration API tests**

Run: `gcc -I components/crt_fb/include -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_fb_cal_test.c components/crt_fb/crt_fb.c components/crt_fb/crt_fb_cal.c -o /tmp/crt_fb_cal_test && /tmp/crt_fb_cal_test`
Expected: `ALL PASSED`

- [ ] **Step 3: Re-run ABI and timing safety tests**

Run: `gcc -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_scanline_abi_test.c -o /tmp/crt_scanline_abi_test && /tmp/crt_scanline_abi_test`
Expected: `ALL PASSED`

Run: `gcc -I components/crt_core/include -I components/crt_timing/include -I tests/stubs tests/crt_scanline_header_test.c -o /tmp/crt_scanline_header_test && /tmp/crt_scanline_header_test`
Expected: success

Run: `gcc -I components/crt_timing/include -I tests/stubs tests/crt_timing_profile_test.c components/crt_timing/crt_timing.c -o /tmp/crt_timing_test && /tmp/crt_timing_test`
Expected: success

- [ ] **Step 4: Rebuild firmware**

Run: `bash -c '. ~/esp/esp-idf/export.sh && idf.py build'`
Expected: successful build

- [ ] **Step 5: Commit the verification pass**

```bash
git add README.md tools/crt_cam_setup.sh tools/analysis
git commit -m "test: verify luma calibration pipeline"
```
