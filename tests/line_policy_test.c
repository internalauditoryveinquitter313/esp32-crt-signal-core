#include <assert.h>
#include <stdbool.h>

#include "crt_line_policy.h"

static void test_sync_width_tracks_line_type(void)
{
    crt_timing_profile_t timing = {
        .sync_width = 67,
        .vsync_width = 845,
    };

    assert(crt_line_policy_sync_width(&timing, CRT_TIMING_LINE_TYPE_ACTIVE) == 67);
    assert(crt_line_policy_sync_width(&timing, CRT_TIMING_LINE_TYPE_BLANK) == 67);
    assert(crt_line_policy_sync_width(&timing, CRT_TIMING_LINE_TYPE_VSYNC) == 845);
}

static void test_burst_presence_skips_only_vsync(void)
{
    assert(crt_line_policy_has_burst(CRT_TIMING_LINE_TYPE_ACTIVE));
    assert(crt_line_policy_has_burst(CRT_TIMING_LINE_TYPE_BLANK));
    assert(!crt_line_policy_has_burst(CRT_TIMING_LINE_TYPE_VSYNC));
}

int main(void)
{
    test_sync_width_tracks_line_type();
    test_burst_presence_skips_only_vsync();
    return 0;
}
