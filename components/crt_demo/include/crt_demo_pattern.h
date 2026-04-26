#pragma once

#include "crt_timing_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRT_DEMO_PATTERN_LOGICAL_WIDTH 256U

typedef enum {
    CRT_DEMO_PATTERN_DISABLED = 0,
    CRT_DEMO_PATTERN_COLOR_BARS_RAMP = 1,
    CRT_DEMO_PATTERN_LUMA_BARS = 2,
} crt_demo_pattern_mode_t;

typedef struct {
    crt_demo_pattern_mode_t mode;
    uint16_t active_line_count;
    uint16_t ramp_height_lines;
    uint8_t color_bars_row[CRT_DEMO_PATTERN_LOGICAL_WIDTH];
    uint8_t grayscale_ramp_row[CRT_DEMO_PATTERN_LOGICAL_WIDTH];
} crt_demo_pattern_runtime_t;

typedef struct {
    crt_video_standard_t video_standard;
    uint16_t line_index;
    uint16_t active_line_index;
    bool in_active;
} crt_demo_pattern_render_context_t;

void crt_demo_pattern_build_color_bars_row(uint8_t *pixels, size_t width);
void crt_demo_pattern_build_grayscale_ramp_row(uint8_t *pixels, size_t width);
bool crt_demo_pattern_is_ramp_region(const crt_demo_pattern_runtime_t *runtime,
                                     uint16_t active_line_index);
void crt_demo_pattern_runtime_init(crt_demo_pattern_runtime_t *runtime,
                                   crt_demo_pattern_mode_t mode, uint16_t active_line_count);
void crt_demo_pattern_render_active_window(const crt_demo_pattern_runtime_t *runtime,
                                           const crt_demo_pattern_render_context_t *ctx,
                                           uint16_t blank_level, uint16_t *samples,
                                           size_t sample_count);

#ifdef __cplusplus
}
#endif
