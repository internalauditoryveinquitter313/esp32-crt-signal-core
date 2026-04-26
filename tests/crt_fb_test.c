/**
 * @file crt_fb_test.c
 * @brief Host-compiled test for crt_fb surface, palette, and scanline hook.
 *
 * gcc -I components/crt_fb/include -I components/crt_core/include \
 *     -I components/crt_timing/include -I tests/stubs \
 *     tests/crt_fb_test.c components/crt_fb/crt_fb.c \
 *     -o /tmp/crt_fb_test
 */

#include "crt_fb.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BLANK_LEVEL ((uint16_t)(23U << 8))
#define WHITE_LEVEL ((uint16_t)(0xFFU << 8))

/* ── Surface lifecycle ────────────────────────────────────────────── */

static void test_init_alloc_free(void)
{
    crt_fb_surface_t s;
    assert(crt_fb_surface_init(&s, 64, 48, CRT_FB_FORMAT_INDEXED8) == 0);
    assert(s.width == 64);
    assert(s.height == 48);
    assert(s.buffer == NULL);
    assert(s.palette == NULL);

    assert(crt_fb_surface_alloc(&s) == 0);
    assert(s.buffer != NULL);
    assert(s.buffer_size == 64 * 48);
    assert(s.palette != NULL);
    assert(s.palette_size == 256);

    /* Double alloc should fail */
    assert(crt_fb_surface_alloc(&s) != 0);

    assert(crt_fb_surface_free(&s) == 0);
    assert(s.buffer == NULL);
    assert(s.palette == NULL);

    assert(crt_fb_surface_deinit(&s) == 0);
    printf("  init/alloc/free: OK\n");
}

/* ── Pixel access ─────────────────────────────────────────────────── */

static void test_pixel_access(void)
{
    crt_fb_surface_t s;
    crt_fb_surface_init(&s, 16, 8, CRT_FB_FORMAT_INDEXED8);
    crt_fb_surface_alloc(&s);

    /* Buffer starts zeroed */
    assert(crt_fb_get(&s, 0, 0) == 0);
    assert(crt_fb_get(&s, 15, 7) == 0);

    crt_fb_put(&s, 5, 3, 42);
    assert(crt_fb_get(&s, 5, 3) == 42);

    /* Out of bounds: no crash, returns 0 */
    assert(crt_fb_get(&s, 16, 0) == 0);
    assert(crt_fb_get(&s, 0, 8) == 0);
    crt_fb_put(&s, 16, 0, 99); /* should be no-op */

    /* Row accessor */
    uint8_t *row = crt_fb_row(&s, 3);
    assert(row != NULL);
    assert(row[5] == 42);
    assert(crt_fb_row(&s, 8) == NULL); /* out of bounds */

    /* Clear */
    crt_fb_clear(&s, 0xAA);
    assert(crt_fb_get(&s, 0, 0) == 0xAA);
    assert(crt_fb_get(&s, 15, 7) == 0xAA);

    crt_fb_surface_deinit(&s);
    printf("  pixel access: OK\n");
}

/* ── Palette ──────────────────────────────────────────────────────── */

static void test_palette_grayscale(void)
{
    crt_fb_surface_t s;
    crt_fb_surface_init(&s, 4, 4, CRT_FB_FORMAT_INDEXED8);
    crt_fb_surface_alloc(&s);

    crt_fb_palette_init_grayscale(&s, BLANK_LEVEL, WHITE_LEVEL);

    /* Index 0 = blank, index 255 = white */
    assert(s.palette[0] == BLANK_LEVEL);
    assert(s.palette[255] == WHITE_LEVEL);

    /* Middle should be between blank and white */
    assert(s.palette[128] > BLANK_LEVEL);
    assert(s.palette[128] < WHITE_LEVEL);

    /* Manual set */
    crt_fb_palette_set(&s, 0, 0x1234);
    assert(s.palette[0] == 0x1234);

    crt_fb_surface_deinit(&s);
    printf("  palette grayscale: OK\n");
}

/* ── Scanline hook ────────────────────────────────────────────────── */

static void test_layer_fetch_adapter(void)
{
    crt_fb_surface_t s;
    crt_fb_surface_init(&s, 4, 2, CRT_FB_FORMAT_INDEXED8);
    crt_fb_surface_alloc(&s);

    /* Paint row 1: [10, 20, 30, 40] */
    crt_fb_put(&s, 0, 1, 10);
    crt_fb_put(&s, 1, 1, 20);
    crt_fb_put(&s, 2, 1, 30);
    crt_fb_put(&s, 3, 1, 40);

    uint8_t out[8];
    memset(out, 0xFF, sizeof(out));

    /* Width 8 (2x scale) */
    crt_fb_layer_fetch(&s, 1, out, 8);
    assert(out[0] == 10 && out[1] == 10);
    assert(out[2] == 20 && out[3] == 20);
    assert(out[4] == 30 && out[5] == 30);
    assert(out[6] == 40 && out[7] == 40);

    /* Line out of bounds -> zero fill */
    memset(out, 0xFF, sizeof(out));
    crt_fb_layer_fetch(&s, 99, out, 8);
    for (int i = 0; i < 8; ++i)
        assert(out[i] == 0);

    /* Null surface -> zero fill */
    memset(out, 0xFF, sizeof(out));
    crt_fb_layer_fetch(NULL, 0, out, 8);
    for (int i = 0; i < 8; ++i)
        assert(out[i] == 0);

    crt_fb_surface_deinit(&s);
    printf("  layer fetch adapter: OK\n");
}

static void test_scanline_hook(void)
{
    crt_fb_surface_t s;
    crt_fb_surface_init(&s, 4, 4, CRT_FB_FORMAT_INDEXED8);
    crt_fb_surface_alloc(&s);
    crt_fb_palette_init_grayscale(&s, BLANK_LEVEL, WHITE_LEVEL);

    /* Paint row 2: [0, 85, 170, 255] */
    crt_fb_put(&s, 0, 2, 0);
    crt_fb_put(&s, 1, 2, 85);
    crt_fb_put(&s, 2, 2, 170);
    crt_fb_put(&s, 3, 2, 255);

    /* Simulate hook call: logical_line 2, active_width 8 (2x scale) */
    crt_timing_profile_t timing;
    memset(&timing, 0, sizeof(timing));
    timing.total_lines = 262;
    timing.active_lines = 240;

    crt_scanline_t sc = {
        .physical_line = 22,
        .logical_line = 2,
        .type = CRT_LINE_ACTIVE,
        .field = 0,
        .frame_number = 0,
        .subcarrier_phase = 0,
        .timing = &timing,
    };

    uint16_t active_buf[8];
    memset(active_buf, 0, sizeof(active_buf));

    crt_fb_scanline_hook(&sc, active_buf, 8, &s);

    /* Nearest-neighbor: each fb pixel maps to 2 DAC samples */
    assert(active_buf[0] == s.palette[0]);
    assert(active_buf[1] == s.palette[0]);
    assert(active_buf[2] == s.palette[85]);
    assert(active_buf[3] == s.palette[85]);
    assert(active_buf[4] == s.palette[170]);
    assert(active_buf[5] == s.palette[170]);
    assert(active_buf[6] == s.palette[255]);
    assert(active_buf[7] == s.palette[255]);

    /* Non-active line should be no-op */
    crt_scanline_t blank_sc = {
        .physical_line = 250,
        .logical_line = CRT_SCANLINE_LOGICAL_LINE_NONE,
        .type = CRT_LINE_BLANK,
        .timing = &timing,
    };
    uint16_t blank_buf[4] = {0xDEAD, 0xDEAD, 0xDEAD, 0xDEAD};
    crt_fb_scanline_hook(&blank_sc, blank_buf, 4, &s);
    assert(blank_buf[0] == 0xDEAD); /* untouched */

    /* Invalid hook inputs should be no-op, not crashes/division by zero. */
    uint16_t guard_buf[4] = {0xBEEF, 0xBEEF, 0xBEEF, 0xBEEF};
    crt_fb_scanline_hook(NULL, guard_buf, 4, &s);
    crt_fb_scanline_hook(&sc, NULL, 4, &s);
    crt_fb_scanline_hook(&sc, guard_buf, 0, &s);
    crt_fb_scanline_hook(&sc, guard_buf, 4, NULL);
    assert(guard_buf[0] == 0xBEEF);

    crt_fb_surface_deinit(&s);
    printf("  scanline hook: OK\n");
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void)
{
    printf("crt_fb test\n");
    test_init_alloc_free();
    test_pixel_access();
    test_palette_grayscale();
    test_layer_fetch_adapter();
    test_scanline_hook();
    printf("ALL PASSED\n");
    return 0;
}
