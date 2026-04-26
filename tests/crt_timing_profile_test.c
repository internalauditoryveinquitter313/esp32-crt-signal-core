#include "crt_timing.h"

#include <assert.h>

static void test_ntsc_profile_matches_legacy_4x_colorburst_timing(void)
{
    crt_timing_profile_t profile = {0};

    assert(crt_timing_get_profile(CRT_VIDEO_STANDARD_NTSC, &profile) == ESP_OK);
    assert(profile.active_start_line == 0);
    assert(profile.active_lines == 240);
    assert(profile.vsync_start_line == 243);
    assert(profile.vsync_line_count == 6);
    assert(profile.samples_per_line == 912);
    assert(profile.sync_width == 64);
    assert(profile.vsync_width == 392);
    assert(profile.vsync_short_width == 0);
    assert(profile.burst_offset == 64);
    assert(profile.burst_width == 40);
    assert(profile.active_offset == 144);
    assert((uint32_t)profile.active_offset + (uint32_t)profile.active_width <=
           (uint32_t)profile.samples_per_line);
}

static void test_pal_profile_matches_legacy_4x_colorburst_timing(void)
{
    crt_timing_profile_t profile = {0};

    assert(crt_timing_get_profile(CRT_VIDEO_STANDARD_PAL, &profile) == ESP_OK);
    assert(profile.active_start_line == 32);
    assert(profile.active_lines == 240);
    assert(profile.vsync_start_line == 304);
    assert(profile.vsync_line_count == 8);
    assert(profile.samples_per_line == 1136);
    assert(profile.sync_width == 80);
    assert(profile.vsync_width == 536);
    assert(profile.vsync_short_width == 32);
    assert(profile.burst_offset == 96);
    assert(profile.burst_width == 44);
    assert(profile.active_offset == 184);
    assert((uint32_t)profile.active_offset + (uint32_t)profile.active_width <=
           (uint32_t)profile.samples_per_line);
}

static void test_active_line_index_is_timing_owned(void)
{
    crt_timing_profile_t ntsc = {0};
    crt_timing_profile_t pal = {0};
    uint16_t active_line = 0xFFFFU;

    assert(crt_timing_get_profile(CRT_VIDEO_STANDARD_NTSC, &ntsc) == ESP_OK);
    assert(crt_timing_get_profile(CRT_VIDEO_STANDARD_PAL, &pal) == ESP_OK);

    assert(crt_timing_get_active_line_index(&ntsc, 0, &active_line));
    assert(active_line == 0);
    assert(crt_timing_get_active_line_index(&ntsc, 239, &active_line));
    assert(active_line == 239);
    assert(!crt_timing_get_active_line_index(&ntsc, 240, &active_line));
    assert(crt_timing_get_first_blank_line_after_active(&ntsc) == 240);

    assert(!crt_timing_get_active_line_index(&pal, 31, &active_line));
    assert(crt_timing_get_active_line_index(&pal, 32, &active_line));
    assert(active_line == 0);
    assert(crt_timing_get_active_line_index(&pal, 271, &active_line));
    assert(active_line == 239);
    assert(!crt_timing_get_active_line_index(&pal, 272, &active_line));
    assert(crt_timing_get_first_blank_line_after_active(&pal) == 272);

    assert(crt_timing_get_profile_line_type(&ntsc, 0) == CRT_TIMING_LINE_TYPE_ACTIVE);
    assert(crt_timing_get_profile_line_type(&ntsc, 240) == CRT_TIMING_LINE_TYPE_BLANK);
    assert(crt_timing_get_profile_line_type(&pal, 31) == CRT_TIMING_LINE_TYPE_BLANK);
    assert(crt_timing_get_profile_line_type(&pal, 32) == CRT_TIMING_LINE_TYPE_ACTIVE);
}

static void test_pal_vsync_window_is_profile_owned(void)
{
    int vsync_lines = 0;

    for (uint16_t line = 0; line < 312; ++line) {
        if (crt_timing_get_line_type(CRT_VIDEO_STANDARD_PAL, line) == CRT_TIMING_LINE_TYPE_VSYNC) {
            ++vsync_lines;
        }
    }

    assert(vsync_lines == 8);
    assert(crt_timing_get_line_type(CRT_VIDEO_STANDARD_PAL, 303) == CRT_TIMING_LINE_TYPE_BLANK);
    assert(crt_timing_get_line_type(CRT_VIDEO_STANDARD_PAL, 304) == CRT_TIMING_LINE_TYPE_VSYNC);
    assert(crt_timing_get_line_type(CRT_VIDEO_STANDARD_PAL, 311) == CRT_TIMING_LINE_TYPE_VSYNC);
}

static void test_ntsc_vsync_window_is_wide_enough_for_stable_vertical_lock(void)
{
    int vsync_lines = 0;

    for (uint16_t line = 0; line < 262; ++line) {
        if (crt_timing_get_line_type(CRT_VIDEO_STANDARD_NTSC, line) == CRT_TIMING_LINE_TYPE_VSYNC) {
            ++vsync_lines;
        }
    }

    assert(vsync_lines == 6);
    assert(crt_timing_get_line_type(CRT_VIDEO_STANDARD_NTSC, 242) == CRT_TIMING_LINE_TYPE_BLANK);
    assert(crt_timing_get_line_type(CRT_VIDEO_STANDARD_NTSC, 243) == CRT_TIMING_LINE_TYPE_VSYNC);
    assert(crt_timing_get_line_type(CRT_VIDEO_STANDARD_NTSC, 248) == CRT_TIMING_LINE_TYPE_VSYNC);
    assert(crt_timing_get_line_type(CRT_VIDEO_STANDARD_NTSC, 249) == CRT_TIMING_LINE_TYPE_BLANK);
}

int main(void)
{
    test_ntsc_profile_matches_legacy_4x_colorburst_timing();
    test_pal_profile_matches_legacy_4x_colorburst_timing();
    test_ntsc_vsync_window_is_wide_enough_for_stable_vertical_lock();
    test_active_line_index_is_timing_owned();
    test_pal_vsync_window_is_profile_owned();
    return 0;
}
