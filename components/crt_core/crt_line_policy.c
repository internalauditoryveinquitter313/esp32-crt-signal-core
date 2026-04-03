#include "crt_line_policy.h"

size_t crt_line_policy_sync_width(const crt_timing_profile_t *timing, crt_timing_line_type_t line_type)
{
    if (line_type == CRT_TIMING_LINE_TYPE_VSYNC) {
        return timing->vsync_width;
    }

    return timing->sync_width;
}

bool crt_line_policy_has_burst(crt_timing_line_type_t line_type)
{
    (void)line_type;
    return false;
}
