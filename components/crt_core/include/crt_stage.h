#pragma once

#include "crt_timing_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    crt_video_standard_t video_standard;
    crt_timing_line_type_t line_type;
    uint16_t line_index;
    uint16_t total_lines;
    uint16_t active_line_index;
    uint16_t active_line_count;
    uint16_t active_offset;
    uint16_t active_width;
    uint16_t burst_offset;
    uint16_t burst_width;
    bool in_vblank;
    bool in_active;
} crt_line_context_t;

typedef struct {
    uint16_t *samples;
    size_t sample_count;
} crt_line_buffer_t;

typedef void (*crt_stage_fn_t)(const crt_line_context_t *ctx, crt_line_buffer_t *line,
                               void *user_ctx);

#ifdef __cplusplus
}
#endif
