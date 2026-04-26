/**
 * @file crt_compose_test.c
 * @brief Host-compiled test for crt_compose layer mixer.
 *
 * gcc -I components/crt_compose/include -I components/crt_core/include \
 *     -I components/crt_timing/include -I tests/stubs \
 *     tests/crt_compose_test.c components/crt_compose/crt_compose.c \
 *     -o /tmp/crt_compose_test && /tmp/crt_compose_test
 */

#include "crt_compose.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

static uint16_t g_palette[256];

static void init_linear_palette(void)
{
    for (int i = 0; i < 256; ++i) {
        g_palette[i] = (uint16_t)(i << 8);
    }
}

static crt_scanline_t make_active_line(uint16_t logical)
{
    static crt_timing_profile_t timing;
    memset(&timing, 0, sizeof(timing));
    timing.total_lines = 262;
    timing.active_lines = 240;
    return (crt_scanline_t)
    {
        .physical_line = logical + 20,
        .logical_line = logical,
        .type = CRT_LINE_ACTIVE,
        .field = 0,
        .frame_number = 0,
        .subcarrier_phase = 0,
        .timing = &timing,
    };
}

/* Fills line with a fixed pattern derived from logical_line + offset. */
typedef struct {
    uint8_t base;
    uint8_t step;
} pattern_ctx_t;

static bool pattern_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width) {
    const pattern_ctx_t *p = (const pattern_ctx_t *)ctx;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = (uint8_t)(p->base + (logical_line * p->step) + (uint8_t)x);
    }
    return true;
}

/* Writes a keyed sprite: every 3rd pixel is opaque, rest is key=0. */
typedef struct {
    uint8_t value;
    uint8_t key;
} sprite_ctx_t;

static bool sprite_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width) {
    const sprite_ctx_t *s = (const sprite_ctx_t *)ctx;
    (void)logical_line;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = (x % 3 == 0) ? s->value : s->key;
    }
    return true;
}

/* ── Fused-path mocks ─────────────────────────────────────────────── */

static uint32_t g_mock_base_fetch_calls;
static uint32_t g_mock_base_override_calls;
static uint32_t g_mock_absent_overlay_calls;

/* mock_base_fetch: writes `(x + y) & 0xFF` per pixel. */
static bool mock_base_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width) {
    (void)ctx;
    g_mock_base_fetch_calls++;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = (uint8_t)(x + logical_line);
    }
    return true;
}

/* mock_base_override: writes palette[(x + y) & 0xFF] with the same word-swap
 * as the generic compose output pass. Guarantees bit-exact parity with
 * mock_base_fetch + palette + swap. */
static void mock_base_override(const crt_scanline_t *scanline, uint16_t *active_buf,
                               uint16_t active_width, void *user_data) {
    (void)user_data;
    g_mock_base_override_calls++;
    const uint16_t *pal = g_palette;
    const uint16_t y = scanline->logical_line;
    const uint16_t even_width = active_width & (uint16_t)~1U;
    uint16_t i = 0;
    for (; i < even_width; i += 2) {
        uint16_t p0 = pal[(uint8_t)(i + y)];
        uint16_t p1 = pal[(uint8_t)(i + 1 + y)];
        active_buf[i] = p1;
        active_buf[i + 1] = p0;
    }
    if (i < active_width) {
        active_buf[i] = pal[(uint8_t)(i + y)];
    }
}

/* mock_absent_overlay_fetch: keyed layer that never contributes. Must NOT
 * touch idx_out; compose is required to skip merge when false is returned. */
static bool mock_absent_overlay_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out,
                                      uint16_t width) {
    (void) ctx;
    (void) logical_line;
    (void) idx_out;
    (void) width;
    g_mock_absent_overlay_calls++;
    return false;
}

static void reset_mock_counters(void)
{
    g_mock_base_fetch_calls = 0;
    g_mock_base_override_calls = 0;
    g_mock_absent_overlay_calls = 0;
}

/* Tracks how many times the fetch was called. */
typedef struct {
    uint32_t calls;
    uint8_t fill;
} counting_ctx_t;

static bool counting_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width) {
    counting_ctx_t *c = (counting_ctx_t *)ctx;
    (void)logical_line;
    c->calls++;
    memset(idx_out, c->fill, width);
    return true;
}

/* ── Tests ────────────────────────────────────────────────────────── */

static void test_init_and_palette(void)
{
    crt_compose_t c;
    assert(crt_compose_init(&c) == 0);
    assert(c.layer_count == 0);
    assert(c.palette == NULL);

    init_linear_palette();
    assert(crt_compose_set_palette(&c, g_palette, 256) == 0);
    assert(c.palette == g_palette);
    assert(c.palette_size == 256);

    assert(crt_compose_set_palette(&c, g_palette, 255) == ESP_ERR_INVALID_SIZE);
    assert(c.palette == g_palette);
    assert(c.palette_size == 256);

    assert(crt_compose_set_palette(&c, NULL, 0) == 0);
    assert(c.palette == NULL);
    assert(c.palette_size == 0);

    assert(crt_compose_set_palette(&c, g_palette, 256) == 0);
    printf("  init/palette: OK\n");
}

static void test_add_layer_limits(void)
{
    crt_compose_t c;
    crt_compose_init(&c);

    pattern_ctx_t pc = {.base = 0, .step = 1};
    for (uint8_t i = 0; i < CRT_COMPOSE_MAX_LAYERS; ++i) {
        assert(crt_compose_add_layer(&c, pattern_fetch, &pc, CRT_COMPOSE_NO_TRANSPARENCY) == 0);
    }
    /* One past max should fail */
    assert(crt_compose_add_layer(&c, pattern_fetch, &pc, CRT_COMPOSE_NO_TRANSPARENCY) != 0);
    assert(c.layer_count == CRT_COMPOSE_MAX_LAYERS);

    crt_compose_clear_layers(&c);
    assert(c.layer_count == 0);
    printf("  add/clear layers: OK\n");
}

static void test_single_opaque_layer_with_swap(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);

    pattern_ctx_t pc = {.base = 10, .step = 0}; /* line = 10, 11, 12, 13, ... */
    assert(crt_compose_add_layer(&c, pattern_fetch, &pc, CRT_COMPOSE_NO_TRANSPARENCY) == 0);

    uint16_t active_buf[8] = {0};
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, active_buf, 8, &c);

    /* Expected indexed line: 10,11,12,13,14,15,16,17
     * After word-swap (pairs):
     *   active_buf[0] = pal[11], active_buf[1] = pal[10]
     *   active_buf[2] = pal[13], active_buf[3] = pal[12]
     *   active_buf[4] = pal[15], active_buf[5] = pal[14]
     *   active_buf[6] = pal[17], active_buf[7] = pal[16]
     */
    assert(active_buf[0] == g_palette[11]);
    assert(active_buf[1] == g_palette[10]);
    assert(active_buf[2] == g_palette[13]);
    assert(active_buf[3] == g_palette[12]);
    assert(active_buf[4] == g_palette[15]);
    assert(active_buf[5] == g_palette[14]);
    assert(active_buf[6] == g_palette[17]);
    assert(active_buf[7] == g_palette[16]);
    printf("  single opaque + word-swap: OK\n");
}

static void test_odd_width_tail(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);

    pattern_ctx_t pc = {.base = 0, .step = 0}; /* 0,1,2 */
    crt_compose_add_layer(&c, pattern_fetch, &pc, CRT_COMPOSE_NO_TRANSPARENCY);

    uint16_t active_buf[3] = {0xDEAD, 0xDEAD, 0xDEAD};
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, active_buf, 3, &c);

    assert(active_buf[0] == g_palette[1]);
    assert(active_buf[1] == g_palette[0]);
    assert(active_buf[2] == g_palette[2]); /* tail, un-swapped */
    printf("  odd-width tail: OK\n");
}

static void test_transparent_overlay(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);

    /* Layer 0: solid 50 (opaque) */
    counting_ctx_t bg = {.calls = 0, .fill = 50};
    crt_compose_add_layer(&c, counting_fetch, &bg, CRT_COMPOSE_NO_TRANSPARENCY);

    /* Layer 1: sprite value=99, key=0; pattern= 99,0,0,99,0,0,99,0 */
    sprite_ctx_t spr = {.value = 99, .key = 0};
    crt_compose_add_layer(&c, sprite_fetch, &spr, 0);

    uint16_t active_buf[8] = {0};
    crt_scanline_t sc = make_active_line(5);
    crt_compose_scanline_hook(&sc, active_buf, 8, &c);

    /* Composed indexed line: 99,50,50,99,50,50,99,50
     * After word-swap:
     *  [0]=pal[50], [1]=pal[99]
     *  [2]=pal[99], [3]=pal[50]
     *  [4]=pal[50], [5]=pal[50]
     *  [6]=pal[50], [7]=pal[99]
     */
    assert(active_buf[0] == g_palette[50]);
    assert(active_buf[1] == g_palette[99]);
    assert(active_buf[2] == g_palette[99]);
    assert(active_buf[3] == g_palette[50]);
    assert(active_buf[4] == g_palette[50]);
    assert(active_buf[5] == g_palette[50]);
    assert(active_buf[6] == g_palette[50]);
    assert(active_buf[7] == g_palette[99]);

    assert(bg.calls == 1);
    printf("  transparent overlay z-order: OK\n");
}

static void test_disabled_layer_skipped(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);
    crt_compose_set_clear_index(&c, 7);

    counting_ctx_t ctx = {.calls = 0, .fill = 200};
    crt_compose_add_layer(&c, counting_fetch, &ctx, CRT_COMPOSE_NO_TRANSPARENCY);
    crt_compose_set_layer_enabled(&c, 0, false);

    uint16_t active_buf[4] = {0};
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, active_buf, 4, &c);

    /* Disabled layer not called; line is clear_idx everywhere = 7 */
    assert(ctx.calls == 0);
    assert(active_buf[0] == g_palette[7]);
    assert(active_buf[1] == g_palette[7]);
    assert(active_buf[2] == g_palette[7]);
    assert(active_buf[3] == g_palette[7]);
    printf("  disabled layer + clear_idx: OK\n");
}

static bool absent_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width) {
    uint32_t *calls = (uint32_t *)ctx;
    (void)logical_line;
    (void)idx_out;
    (void)width;
    (*calls)++;
    /* Deliberately do not touch idx_out: compose must not read it back. */
    return false;
}

static void test_keyed_absent_skips_merge(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);

    /* Layer 0: solid 77 (opaque BG) */
    counting_ctx_t bg = {.calls = 0, .fill = 77};
    crt_compose_add_layer(&c, counting_fetch, &bg, CRT_COMPOSE_NO_TRANSPARENCY);

    /* Layer 1: keyed overlay that reports "not present" */
    uint32_t absent_calls = 0;
    crt_compose_add_layer(&c, absent_fetch, &absent_calls, 0);

    uint16_t active_buf[4] = {0};
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, active_buf, 4, &c);

    /* Overlay was queried but its contents were ignored -> BG shows through. */
    assert(absent_calls == 1);
    assert(active_buf[0] == g_palette[77]);
    assert(active_buf[1] == g_palette[77]);
    assert(active_buf[2] == g_palette[77]);
    assert(active_buf[3] == g_palette[77]);
    printf("  keyed absent fetch skips merge: OK\n");
}

static void test_opaque_base_skips_clear(void)
{
    /* Ensure opaque layer 0 fully controls the line content regardless of
     * clear_idx (i.e. compose does not pre-fill and then overwrite). */
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);
    crt_compose_set_clear_index(&c, 33); /* would be visible if clear happened */

    counting_ctx_t bg = {.calls = 0, .fill = 101};
    crt_compose_add_layer(&c, counting_fetch, &bg, CRT_COMPOSE_NO_TRANSPARENCY);

    uint16_t active_buf[4] = {0};
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, active_buf, 4, &c);

    /* Every pixel should be 101 (from layer), never 33 (clear_idx). */
    assert(active_buf[0] == g_palette[101]);
    assert(active_buf[1] == g_palette[101]);
    assert(active_buf[2] == g_palette[101]);
    assert(active_buf[3] == g_palette[101]);
    assert(bg.calls == 1);
    printf("  opaque base bypasses clear: OK\n");
}

static void test_fused_base_solo_delegates(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);
    reset_mock_counters();

    assert(crt_compose_add_layer_fused(&c, mock_base_fetch, mock_base_override, NULL) == 0);

    uint16_t buf[8] = {0};
    crt_scanline_t sc = make_active_line(3);
    crt_compose_scanline_hook(&sc, buf, 8, &c);

    /* Only the override runs; generic fetch path must stay silent. */
    assert(g_mock_base_override_calls == 1);
    assert(g_mock_base_fetch_calls == 0);

    /* Output produced by the override (pal[(x+y)&0xFF] with word-swap). */
    assert(buf[0] == g_palette[4]);
    assert(buf[1] == g_palette[3]);
    assert(buf[2] == g_palette[6]);
    assert(buf[3] == g_palette[5]);
    assert(buf[4] == g_palette[8]);
    assert(buf[5] == g_palette[7]);
    assert(buf[6] == g_palette[10]);
    assert(buf[7] == g_palette[9]);
    printf("  fused base solo delegates direct: OK\n");
}

static void test_fused_base_plus_absent_overlay_delegates(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);
    reset_mock_counters();

    crt_compose_add_layer_fused(&c, mock_base_fetch, mock_base_override, NULL);
    crt_compose_add_layer(&c, mock_absent_overlay_fetch, NULL, 0);

    uint16_t buf[4] = {0};
    crt_scanline_t sc = make_active_line(5);
    crt_compose_scanline_hook(&sc, buf, 4, &c);

    /* Overlay must be probed exactly once.
     * Nothing contributes -> delegation wins, no materialization. */
    assert(g_mock_absent_overlay_calls == 1);
    assert(g_mock_base_override_calls == 1);
    assert(g_mock_base_fetch_calls == 0);

    /* Output matches what the override writes, not the generic path. */
    assert(buf[0] == g_palette[(uint8_t)(1 + 5)]);
    assert(buf[1] == g_palette[(uint8_t)(0 + 5)]);
    assert(buf[2] == g_palette[(uint8_t)(3 + 5)]);
    assert(buf[3] == g_palette[(uint8_t)(2 + 5)]);
    printf("  fused base + absent overlay still delegates: OK\n");
}

static void test_fused_base_plus_present_overlay_materializes(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);
    reset_mock_counters();

    crt_compose_add_layer_fused(&c, mock_base_fetch, mock_base_override, NULL);
    counting_ctx_t overlay = {.calls = 0, .fill = 99};
    crt_compose_add_layer(&c, counting_fetch, &overlay, 0);

    uint16_t buf[4] = {0};
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, buf, 4, &c);

    /* Overlay contributed -> base materializes, override is NOT called. */
    assert(overlay.calls == 1);
    assert(g_mock_base_fetch_calls == 1);
    assert(g_mock_base_override_calls == 0);

    /* Composed line: overlay wrote 99 everywhere (no transparent pixels),
     * so every slot = 99, then palette+swap. */
    assert(buf[0] == g_palette[99]);
    assert(buf[1] == g_palette[99]);
    assert(buf[2] == g_palette[99]);
    assert(buf[3] == g_palette[99]);
    printf("  fused base + present overlay materializes: OK\n");
}

static void test_fused_vs_generic_parity(void)
{
    /* Same logical content rendered via the override path and via the
     * generic fetch+palette+swap path must produce identical active_buf. */
    init_linear_palette();
    crt_scanline_t sc = make_active_line(7);

    uint16_t buf_fused[16] = {0};
    uint16_t buf_generic[16] = {0};

    {
        crt_compose_t c;
        crt_compose_init(&c);
        crt_compose_set_palette(&c, g_palette, 256);
        crt_compose_add_layer_fused(&c, mock_base_fetch, mock_base_override, NULL);
        crt_compose_scanline_hook(&sc, buf_fused, 16, &c);
    }

    {
        crt_compose_t c;
        crt_compose_init(&c);
        crt_compose_set_palette(&c, g_palette, 256);
        /* No override -> compose takes the generic path. */
        crt_compose_add_layer(&c, mock_base_fetch, NULL, CRT_COMPOSE_NO_TRANSPARENCY);
        crt_compose_scanline_hook(&sc, buf_generic, 16, &c);
    }

    for (int i = 0; i < 16; ++i) {
        assert(buf_fused[i] == buf_generic[i]);
    }
    printf("  fused override parity with generic path: OK\n");
}

static void test_non_active_line_noop(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);

    counting_ctx_t ctx = {.calls = 0, .fill = 1};
    crt_compose_add_layer(&c, counting_fetch, &ctx, CRT_COMPOSE_NO_TRANSPARENCY);

    uint16_t active_buf[4] = {0xBEEF, 0xBEEF, 0xBEEF, 0xBEEF};
    crt_scanline_t blank = {
        .physical_line = 250,
        .logical_line = CRT_SCANLINE_LOGICAL_LINE_NONE,
        .type = CRT_LINE_BLANK,
        .timing = NULL,
    };
    crt_compose_scanline_hook(&blank, active_buf, 4, &c);

    assert(ctx.calls == 0);
    assert(active_buf[0] == 0xBEEF);
    printf("  non-active line is no-op: OK\n");
}

static void test_missing_palette_noop(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    /* No palette set */

    counting_ctx_t ctx = {.calls = 0, .fill = 1};
    crt_compose_add_layer(&c, counting_fetch, &ctx, CRT_COMPOSE_NO_TRANSPARENCY);

    uint16_t active_buf[4] = {0xCAFE, 0xCAFE, 0xCAFE, 0xCAFE};
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, active_buf, 4, &c);

    assert(ctx.calls == 0);
    assert(active_buf[0] == 0xCAFE);
    printf("  missing palette is no-op: OK\n");
}

static void test_width_overflow_guarded(void)
{
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);

    counting_ctx_t ctx = {.calls = 0, .fill = 1};
    crt_compose_add_layer(&c, counting_fetch, &ctx, CRT_COMPOSE_NO_TRANSPARENCY);

    /* active_width > MAX must not invoke fetch or touch buffer */
    uint16_t buf = 0x1234;
    crt_scanline_t sc = make_active_line(0);
    crt_compose_scanline_hook(&sc, &buf, CRT_COMPOSE_MAX_WIDTH + 1, &c);

    assert(ctx.calls == 0);
    assert(buf == 0x1234);
    printf("  oversize width guarded: OK\n");
}

static void test_invalid_hook_inputs_noop(void) {
    crt_compose_t c;
    crt_compose_init(&c);
    init_linear_palette();
    crt_compose_set_palette(&c, g_palette, 256);

    counting_ctx_t ctx = {.calls = 0, .fill = 1};
    crt_compose_add_layer(&c, counting_fetch, &ctx, CRT_COMPOSE_NO_TRANSPARENCY);

    crt_scanline_t sc = make_active_line(0);
    uint16_t buf[4] = {0xBEEF, 0xBEEF, 0xBEEF, 0xBEEF};
    crt_compose_scanline_hook(NULL, buf, 4, &c);
    crt_compose_scanline_hook(&sc, NULL, 4, &c);
    crt_compose_scanline_hook(&sc, buf, 0, &c);
    crt_compose_scanline_hook(&sc, buf, 4, NULL);

    assert(ctx.calls == 0);
    assert(buf[0] == 0xBEEF);
    printf("  invalid hook inputs are no-op: OK\n");
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void)
{
    printf("crt_compose test\n");
    test_init_and_palette();
    test_add_layer_limits();
    test_single_opaque_layer_with_swap();
    test_odd_width_tail();
    test_transparent_overlay();
    test_disabled_layer_skipped();
    test_keyed_absent_skips_merge();
    test_opaque_base_skips_clear();
    test_fused_base_solo_delegates();
    test_fused_base_plus_absent_overlay_delegates();
    test_fused_base_plus_present_overlay_materializes();
    test_fused_vs_generic_parity();
    test_non_active_line_noop();
    test_missing_palette_noop();
    test_width_overflow_guarded();
    test_invalid_hook_inputs_noop();
    printf("ALL PASSED\n");
    return 0;
}
