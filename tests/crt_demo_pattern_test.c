#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "crt_demo_pattern.h"

static void test_color_bars_row_splits_into_eight_regions(void)
{
    uint8_t pixels[16] = {0};

    crt_demo_pattern_build_color_bars_row(pixels, 16);

    for (int i = 0; i < 16; ++i) {
        assert(pixels[i] == (uint8_t)(i / 2));
    }
}

static void test_grayscale_ramp_spans_full_range(void)
{
    uint8_t pixels[5] = {0};

    crt_demo_pattern_build_grayscale_ramp_row(pixels, 5);

    assert(pixels[0] == 0);
    assert(pixels[4] == 255);
    assert(pixels[0] <= pixels[1]);
    assert(pixels[1] <= pixels[2]);
    assert(pixels[2] <= pixels[3]);
    assert(pixels[3] <= pixels[4]);
}

static void test_ramp_region_occupies_bottom_lines_only(void)
{
    crt_demo_pattern_runtime_t runtime = {
        .mode = CRT_DEMO_PATTERN_COLOR_BARS_RAMP,
        .active_line_count = 240,
        .ramp_height_lines = 32,
    };

    assert(!crt_demo_pattern_is_ramp_region(&runtime, 207));
    assert(crt_demo_pattern_is_ramp_region(&runtime, 208));
    assert(crt_demo_pattern_is_ramp_region(&runtime, 239));
}

static void test_ntsc_yellow_bar_uses_legacy_composite_pattern(void)
{
    crt_demo_pattern_runtime_t runtime = {0};
    crt_demo_pattern_render_context_t ctx = {
        .video_standard = CRT_VIDEO_STANDARD_NTSC,
        .line_index = 1,
        .active_line_index = 0,
        .in_active = true,
    };
    uint16_t samples[768] = {0};
    const uint16_t expected[12] = {
        0x454c, 0x4c41, 0x413a, 0x3a00,
        0x454c, 0x4c41, 0x413a, 0x3a00,
        0x454c, 0x4c41, 0x413a, 0x3a00,
    };

    crt_demo_pattern_runtime_init(&runtime, CRT_DEMO_PATTERN_COLOR_BARS_RAMP, 240);
    memset(runtime.color_bars_row, 1, sizeof(runtime.color_bars_row));
    crt_demo_pattern_render_active_window(&runtime, &ctx, 0, samples, 768);
    assert(memcmp(samples, expected, sizeof(expected)) == 0);
}

static void test_pal_yellow_bar_alternates_legacy_phase_by_line(void)
{
    crt_demo_pattern_runtime_t runtime = {0};
    crt_demo_pattern_render_context_t even_ctx = {
        .video_standard = CRT_VIDEO_STANDARD_PAL,
        .line_index = 0,
        .active_line_index = 0,
        .in_active = true,
    };
    crt_demo_pattern_render_context_t odd_ctx = {
        .video_standard = CRT_VIDEO_STANDARD_PAL,
        .line_index = 1,
        .active_line_index = 0,
        .in_active = true,
    };
    uint16_t even_samples[768] = {0};
    uint16_t odd_samples[768] = {0};
    const uint16_t expected_even[12] = {
        0x4751, 0x5140, 0x4035, 0x3500,
        0x4751, 0x5140, 0x4035, 0x3500,
        0x4751, 0x5140, 0x4035, 0x3500,
    };
    const uint16_t expected_odd[12] = {
        0x4051, 0x5147, 0x4735, 0x3500,
        0x4051, 0x5147, 0x4735, 0x3500,
        0x4051, 0x5147, 0x4735, 0x3500,
    };

    crt_demo_pattern_runtime_init(&runtime, CRT_DEMO_PATTERN_COLOR_BARS_RAMP, 240);
    memset(runtime.color_bars_row, 1, sizeof(runtime.color_bars_row));
    crt_demo_pattern_render_active_window(&runtime, &even_ctx, 0, even_samples, 768);
    crt_demo_pattern_render_active_window(&runtime, &odd_ctx, 0, odd_samples, 768);
    assert(memcmp(even_samples, expected_even, sizeof(expected_even)) == 0);
    assert(memcmp(odd_samples, expected_odd, sizeof(expected_odd)) == 0);
}

static void test_standard_marker_differs_between_ntsc_and_pal(void)
{
    crt_demo_pattern_runtime_t runtime = {0};
    crt_demo_pattern_render_context_t ntsc_ctx = {
        .video_standard = CRT_VIDEO_STANDARD_NTSC,
        .line_index = 0,
        .active_line_index = 0,
        .in_active = true,
    };
    crt_demo_pattern_render_context_t pal_ctx = {
        .video_standard = CRT_VIDEO_STANDARD_PAL,
        .line_index = 0,
        .active_line_index = 0,
        .in_active = true,
    };
    uint16_t ntsc_samples[768] = {0};
    uint16_t pal_samples[768] = {0};
    const size_t marker_pixel = 16U; /* Top-left marker area: lit on 'N', dark on 'P'. */
    const size_t marker_sample = (marker_pixel * 768U) / CRT_DEMO_PATTERN_LOGICAL_WIDTH;

    crt_demo_pattern_runtime_init(&runtime, CRT_DEMO_PATTERN_COLOR_BARS_RAMP, 240);
    memset(runtime.color_bars_row, 7, sizeof(runtime.color_bars_row));
    crt_demo_pattern_render_active_window(&runtime, &ntsc_ctx, 0, ntsc_samples, 768);
    crt_demo_pattern_render_active_window(&runtime, &pal_ctx, 0, pal_samples, 768);

    assert(ntsc_samples[marker_sample] == 18176U);
    assert(pal_samples[marker_sample] != ntsc_samples[marker_sample]);
}

int main(void)
{
    test_color_bars_row_splits_into_eight_regions();
    test_grayscale_ramp_spans_full_range();
    test_ramp_region_occupies_bottom_lines_only();
    test_ntsc_yellow_bar_uses_legacy_composite_pattern();
    test_pal_yellow_bar_alternates_legacy_phase_by_line();
    test_standard_marker_differs_between_ntsc_and_pal();
    return 0;
}
