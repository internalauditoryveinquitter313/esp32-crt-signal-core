#include "crt_line_policy.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

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

static void test_burst_presence_is_active_video_only(void)
{
    assert(crt_line_policy_has_burst(CRT_TIMING_LINE_TYPE_ACTIVE));
    assert(!crt_line_policy_has_burst(CRT_TIMING_LINE_TYPE_BLANK));
    assert(!crt_line_policy_has_burst(CRT_TIMING_LINE_TYPE_VSYNC));
}

static void test_apply_ntsc_vsync_as_single_long_sync_run(void)
{
    uint16_t samples[16];
    crt_timing_profile_t timing = {
        .standard = CRT_VIDEO_STANDARD_NTSC,
        .vsync_width = 6,
    };

    memset(samples, 0x7F, sizeof(samples));
    crt_line_policy_apply_sync(&timing, 243, CRT_TIMING_LINE_TYPE_VSYNC, samples, 16, 0x1234);

    for (size_t i = 0; i < 6; ++i) {
        assert(samples[i] == 0x1234);
    }
    for (size_t i = 6; i < 16; ++i) {
        assert(samples[i] == 0x7F7F);
    }
}

static void test_apply_pal_vsync_half_line_sequence(void)
{
    uint16_t samples[16];
    crt_timing_profile_t timing = {
        .standard = CRT_VIDEO_STANDARD_PAL,
        .vsync_start_line = 304,
        .vsync_width = 6,
        .vsync_short_width = 2,
    };

    memset(samples, 0x7F, sizeof(samples));
    crt_line_policy_apply_sync(&timing, 307, CRT_TIMING_LINE_TYPE_VSYNC, samples, 16, 0x1234);

    for (size_t i = 0; i < 6; ++i) {
        assert(samples[i] == 0x1234);
    }
    for (size_t i = 6; i < 8; ++i) {
        assert(samples[i] == 0x7F7F);
    }
    for (size_t i = 8; i < 14; ++i) {
        assert(samples[i] == 0x1234);
    }
    for (size_t i = 14; i < 16; ++i) {
        assert(samples[i] == 0x7F7F);
    }
}

int main(void)
{
    test_sync_width_tracks_line_type();
    test_burst_presence_is_active_video_only();
    test_apply_ntsc_vsync_as_single_long_sync_run();
    test_apply_pal_vsync_half_line_sequence();
    return 0;
}
