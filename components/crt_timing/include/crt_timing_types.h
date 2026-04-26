#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CRT_VIDEO_STANDARD_NTSC = 0,
    CRT_VIDEO_STANDARD_PAL = 1,
} crt_video_standard_t;

typedef enum {
    CRT_TIMING_LINE_TYPE_ACTIVE = 0,
    CRT_TIMING_LINE_TYPE_BLANK = 1,
    CRT_TIMING_LINE_TYPE_VSYNC = 2,
} crt_timing_line_type_t;

typedef struct {
    crt_video_standard_t standard;
    uint32_t sample_rate_hz;
    uint16_t total_lines;
    uint16_t active_start_line;
    uint16_t active_lines;
    uint16_t vsync_start_line;
    uint16_t vsync_line_count;
    uint16_t samples_per_line;
    uint16_t active_offset;
    uint16_t active_width;
    uint16_t sync_width;
    uint16_t vsync_width;
    uint16_t vsync_short_width;
    uint16_t burst_offset;
    uint16_t burst_width;
} crt_timing_profile_t;

#ifdef __cplusplus
}
#endif
