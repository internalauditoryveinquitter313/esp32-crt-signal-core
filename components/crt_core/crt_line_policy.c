#include "crt_line_policy.h"

size_t crt_line_policy_sync_width(const crt_timing_profile_t *timing,
                                  crt_timing_line_type_t line_type)
{
    if (line_type == CRT_TIMING_LINE_TYPE_VSYNC) {
        return timing->vsync_width;
    }

    return timing->sync_width;
}

bool crt_line_policy_has_burst(crt_timing_line_type_t line_type)
{
    return line_type == CRT_TIMING_LINE_TYPE_ACTIVE;
}

static void crt_line_policy_fill_sync_run(uint16_t *samples, size_t sample_count, size_t offset,
                                          size_t width, uint16_t sync_level)
{
    if (offset >= sample_count) {
        return;
    }
    if (offset + width > sample_count) {
        width = sample_count - offset;
    }
    for (size_t i = 0; i < width; ++i) {
        samples[offset + i] = sync_level;
    }
}

void crt_line_policy_apply_sync(const crt_timing_profile_t *timing, uint16_t line_index,
                                crt_timing_line_type_t line_type, uint16_t *samples,
                                size_t sample_count, uint16_t sync_level)
{
    static const uint8_t k_pal_vsync_type[8] = {0, 0, 0, 3, 3, 2, 0, 0};

    if (timing == NULL || samples == NULL || sample_count == 0) {
        return;
    }

    if (line_type == CRT_TIMING_LINE_TYPE_VSYNC && timing->standard == CRT_VIDEO_STANDARD_PAL &&
        timing->vsync_short_width > 0 && line_index >= timing->vsync_start_line) {
        uint16_t vsync_line = (uint16_t)(line_index - timing->vsync_start_line);

        if (vsync_line < sizeof(k_pal_vsync_type)) {
            uint8_t type = k_pal_vsync_type[vsync_line];
            size_t half_line = sample_count / 2U;
            size_t first_width = (type & 0x2U) ? timing->vsync_width : timing->vsync_short_width;
            size_t second_width = (type & 0x1U) ? timing->vsync_width : timing->vsync_short_width;

            crt_line_policy_fill_sync_run(samples, sample_count, 0, first_width, sync_level);
            crt_line_policy_fill_sync_run(samples, sample_count, half_line, second_width,
                                          sync_level);
            return;
        }
    }

    crt_line_policy_fill_sync_run(samples, sample_count, 0,
                                  crt_line_policy_sync_width(timing, line_type), sync_level);
}
