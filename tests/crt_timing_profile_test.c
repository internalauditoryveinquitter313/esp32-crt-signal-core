#include <assert.h>

#include "crt_timing.h"

static void test_ntsc_profile_matches_legacy_4x_colorburst_timing(void)
{
    crt_timing_profile_t profile = {0};

    assert(crt_timing_get_profile(CRT_VIDEO_STANDARD_NTSC, &profile) == ESP_OK);
    assert(profile.samples_per_line == 912);
    assert(profile.sync_width == 64);
    assert(profile.vsync_width == 840);
    assert(profile.burst_offset == 64);
    assert(profile.burst_width == 40);
    assert(profile.active_offset == 144);
    assert((uint32_t)profile.active_offset + (uint32_t)profile.active_width <= (uint32_t)profile.samples_per_line);
}

static void test_pal_profile_matches_legacy_4x_colorburst_timing(void)
{
    crt_timing_profile_t profile = {0};

    assert(crt_timing_get_profile(CRT_VIDEO_STANDARD_PAL, &profile) == ESP_OK);
    assert(profile.samples_per_line == 1136);
    assert(profile.sync_width == 80);
    assert(profile.vsync_width == 536);
    assert(profile.burst_offset == 96);
    assert(profile.burst_width == 44);
    assert(profile.active_offset == 184);
    assert((uint32_t)profile.active_offset + (uint32_t)profile.active_width <= (uint32_t)profile.samples_per_line);
}

int main(void)
{
    test_ntsc_profile_matches_legacy_4x_colorburst_timing();
    test_pal_profile_matches_legacy_4x_colorburst_timing();
    return 0;
}
