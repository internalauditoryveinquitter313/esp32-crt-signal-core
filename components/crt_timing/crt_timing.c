#include "crt_timing.h"

#include "esp_check.h"

esp_err_t crt_timing_get_profile(crt_video_standard_t standard, crt_timing_profile_t *out_profile)
{
    ESP_RETURN_ON_FALSE(out_profile != NULL, ESP_ERR_INVALID_ARG, "crt_timing", "profile is null");

    switch (standard) {
    case CRT_VIDEO_STANDARD_NTSC:
        *out_profile = (crt_timing_profile_t){
            .standard = CRT_VIDEO_STANDARD_NTSC,
            .sample_rate_hz = 14318180,
            .total_lines = 262,
            .active_start_line = 0,
            .active_lines = 240,
            .vsync_start_line = 243,
            .vsync_line_count = 6,
            .samples_per_line = 912,
            .active_offset = 144,
            .active_width = 768,
            .sync_width = 64,
            .vsync_width = 392,
            .vsync_short_width = 0,
            .burst_offset = 64,
            .burst_width = 40,
        };
        return ESP_OK;
    case CRT_VIDEO_STANDARD_PAL:
        *out_profile = (crt_timing_profile_t){
            .standard = CRT_VIDEO_STANDARD_PAL,
            .sample_rate_hz = 17734476,
            .total_lines = 312,
            .active_start_line = 32,
            .active_lines = 240,
            .vsync_start_line = 304,
            .vsync_line_count = 8,
            .samples_per_line = 1136,
            .active_offset = 184,
            .active_width = 768,
            .sync_width = 80,
            .vsync_width = 536,
            .vsync_short_width = 32,
            .burst_offset = 96,
            .burst_width = 44,
        };
        return ESP_OK;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

crt_timing_line_type_t crt_timing_get_line_type(crt_video_standard_t standard, uint16_t line_index)
{
    crt_timing_profile_t profile = {0};

    if (crt_timing_get_profile(standard, &profile) != ESP_OK) {
        return CRT_TIMING_LINE_TYPE_BLANK;
    }

    return crt_timing_get_profile_line_type(&profile, line_index);
}

crt_timing_line_type_t crt_timing_get_profile_line_type(const crt_timing_profile_t *profile,
                                                        uint16_t line_index)
{
    if (profile == NULL) {
        return CRT_TIMING_LINE_TYPE_BLANK;
    }

    uint16_t active_line_index = 0;
    if (crt_timing_get_active_line_index(profile, line_index, &active_line_index)) {
        return CRT_TIMING_LINE_TYPE_ACTIVE;
    }

    const uint16_t vsync_end = (uint16_t)(profile->vsync_start_line + profile->vsync_line_count);
    if (line_index >= profile->vsync_start_line && line_index < vsync_end) {
        return CRT_TIMING_LINE_TYPE_VSYNC;
    }

    return CRT_TIMING_LINE_TYPE_BLANK;
}

bool crt_timing_get_active_line_index(const crt_timing_profile_t *profile, uint16_t line_index,
                                      uint16_t *out_active_line_index)
{
    if (profile == NULL || out_active_line_index == NULL) {
        return false;
    }

    const uint16_t active_end = (uint16_t)(profile->active_start_line + profile->active_lines);
    if (line_index < profile->active_start_line || line_index >= active_end) {
        return false;
    }

    *out_active_line_index = (uint16_t)(line_index - profile->active_start_line);
    return true;
}

uint16_t crt_timing_get_first_blank_line_after_active(const crt_timing_profile_t *profile)
{
    if (profile == NULL || profile->total_lines == 0) {
        return 0;
    }

    return (uint16_t)((profile->active_start_line + profile->active_lines) % profile->total_lines);
}
