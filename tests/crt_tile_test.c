/**
 * @file crt_tile_test.c
 * @brief Host-compiled test for crt_tile layer (fetch + fused scanline hook).
 *
 * gcc -I components/crt_tile/include -I components/crt_core/include \
 *     -I components/crt_timing/include -I tests/stubs \
 *     tests/crt_tile_test.c components/crt_tile/crt_tile.c \
 *     -o /tmp/crt_tile_test && /tmp/crt_tile_test
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "crt_tile.h"

/* ── Shared palette + scanline helpers ────────────────────────────── */

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
    return (crt_scanline_t) {
        .physical_line = (uint16_t)(logical + 20),
        .logical_line = logical,
        .type = CRT_LINE_ACTIVE,
        .field = 0,
        .frame_number = 0,
        .subcarrier_phase = 0,
        .timing = &timing,
    };
}

/* ── Synthetic pattern + nametable ────────────────────────────────── */

/* Tile index i contains the constant pixel value `i` repeated over
 * the 8x8 cell. Cheap way to verify which tile feeds each output. */
static void fill_constant_patterns(uint8_t *pattern, uint16_t count)
{
    for (uint16_t i = 0; i < count; ++i) {
        memset(&pattern[(size_t)i * 64], (int)i, 64);
    }
}

/* ── Tests ────────────────────────────────────────────────────────── */

static void test_init_validation(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64] = {0};
    uint8_t nt[32 * 30] = {0};

    /* Null state */
    assert(crt_tile_init(NULL, 32, 30, 32, 30, pattern, 1, nt) != 0);
    /* Zero dims */
    assert(crt_tile_init(&t, 0, 30, 32, 30, pattern, 1, nt) != 0);
    assert(crt_tile_init(&t, 32, 0, 32, 30, pattern, 1, nt) != 0);
    /* Pitch < visible */
    assert(crt_tile_init(&t, 32, 30, 16, 30, pattern, 1, nt) != 0);
    assert(crt_tile_init(&t, 32, 30, 32, 16, pattern, 1, nt) != 0);
    /* Null pattern / nametable */
    assert(crt_tile_init(&t, 32, 30, 32, 30, NULL, 1, nt) != 0);
    assert(crt_tile_init(&t, 32, 30, 32, 30, pattern, 1, NULL) != 0);
    /* pattern_count > 256 */
    assert(crt_tile_init(&t, 32, 30, 32, 30, pattern, 257, nt) != 0);

    /* Happy path: 32x30 visible, 32x32 pitch (power of two) */
    uint8_t nt32[32 * 32] = {0};
    assert(crt_tile_init(&t, 32, 30, 32, 32, pattern, 1, nt32) == 0);
    assert(t.pitch_w_mask == 31);  /* PoT -> mask set */
    assert(t.pitch_h_mask == 31);

    /* Non-PoT pitch: masks stay zero, fallback modulo */
    assert(crt_tile_init(&t, 30, 25, 30, 25, pattern, 1, nt) == 0);
    assert(t.pitch_w_mask == 0);
    assert(t.pitch_h_mask == 0);
    printf("  init validation: OK\n");
}

static void test_set_get_tile_roundtrip(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64 * 4] = {0};
    uint8_t nt[32 * 32] = {0};
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 4, nt);

    crt_tile_set_tile(&t, 5, 7, 42);
    assert(crt_tile_get_tile(&t, 5, 7) == 42);
    /* Out of bounds: no crash, get returns 0 */
    crt_tile_set_tile(&t, 100, 100, 99);
    assert(crt_tile_get_tile(&t, 100, 100) == 0);
    assert(crt_tile_get_tile(&t, 5, 7) == 42);
    printf("  set/get tile roundtrip: OK\n");
}

static void test_scroll_normalization(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64] = {0};
    uint8_t nt[32 * 32] = {0};
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 1, nt);

    /* visible 32x30 tiles -> logical 256x240 */
    crt_tile_set_scroll(&t, 10, 20);
    assert(t.scroll_x_px == 10);
    assert(t.scroll_y_px == 20);

    /* Wraparound above upper bound */
    crt_tile_set_scroll(&t, 256 + 5, 240 + 3);
    assert(t.scroll_x_px == 5);
    assert(t.scroll_y_px == 3);

    /* Negative inputs normalized into range */
    crt_tile_set_scroll(&t, -1, -1);
    assert(t.scroll_x_px == 255);
    assert(t.scroll_y_px == 239);

    /* Large negative */
    crt_tile_set_scroll(&t, -256 - 7, -240 - 9);
    assert(t.scroll_x_px == 256 - 7);
    assert(t.scroll_y_px == 240 - 9);
    printf("  scroll normalization: OK\n");
}

static void test_fetch_static_nametable(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64 * 4];
    uint8_t nt[32 * 32];
    memset(nt, 0, sizeof(nt));
    fill_constant_patterns(pattern, 4);
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 4, nt);

    /* Paint nametable columns 0..3 with tile indices 0..3 */
    for (uint16_t c = 0; c < 4; ++c) {
        crt_tile_set_tile(&t, c, 0, (uint8_t)c);
    }
    /* Remaining columns stay 0 */

    uint8_t out[256];
    assert(crt_tile_layer_fetch(&t, 0, out, 256) == true);

    /* Tile 0 fills pixels 0..7 (value 0)
     * Tile 1 fills pixels 8..15 (value 1)
     * Tile 2 fills pixels 16..23 (value 2)
     * Tile 3 fills pixels 24..31 (value 3)
     * Columns 4..31 are tile 0 -> pixel value 0 */
    for (int i = 0; i < 8; ++i)  assert(out[i] == 0);
    for (int i = 8; i < 16; ++i) assert(out[i] == 1);
    for (int i = 16; i < 24; ++i) assert(out[i] == 2);
    for (int i = 24; i < 32; ++i) assert(out[i] == 3);
    for (int i = 32; i < 256; ++i) assert(out[i] == 0);
    printf("  fetch static nametable: OK\n");
}

static void test_fetch_with_scroll_x(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64 * 4];
    uint8_t nt[32 * 32];
    memset(nt, 0, sizeof(nt));
    fill_constant_patterns(pattern, 4);
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 4, nt);

    /* Column 0 = tile 1, rest 0 */
    crt_tile_set_tile(&t, 0, 0, 1);

    uint8_t out[256];

    /* scroll_x = 0: tile 1 visible at pixels 0..7 */
    crt_tile_set_scroll(&t, 0, 0);
    crt_tile_layer_fetch(&t, 0, out, 256);
    for (int i = 0; i < 8; ++i)  assert(out[i] == 1);
    for (int i = 8; i < 16; ++i) assert(out[i] == 0);

    /* scroll_x = 3 shifts start so pixels 0..4 are the tail of tile 1 */
    crt_tile_set_scroll(&t, 3, 0);
    crt_tile_layer_fetch(&t, 0, out, 256);
    for (int i = 0; i < 5; ++i)  assert(out[i] == 1);
    for (int i = 5; i < 13; ++i) assert(out[i] == 0);

    /* scroll_x = 8 skips tile 1 from the start. Columns shown are
     * 1..31 (all tile 0) followed by a wraparound into column 0
     * (tile 1) at pixels 248..255. */
    crt_tile_set_scroll(&t, 8, 0);
    crt_tile_layer_fetch(&t, 0, out, 256);
    for (int i = 0; i < 248; ++i) assert(out[i] == 0);
    for (int i = 248; i < 256; ++i) assert(out[i] == 1);
    printf("  fetch with scroll_x: OK\n");
}

static void test_fetch_with_scroll_y_crosses_tile(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64 * 4];
    uint8_t nt[32 * 32];
    memset(nt, 0, sizeof(nt));
    fill_constant_patterns(pattern, 4);
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 4, nt);

    /* Row 0 = tile 1, row 1 = tile 2 */
    for (uint16_t c = 0; c < 32; ++c) {
        crt_tile_set_tile(&t, c, 0, 1);
        crt_tile_set_tile(&t, c, 1, 2);
    }

    uint8_t out[256];

    /* logical_line=7 without scroll: still row 0 -> tile 1 */
    crt_tile_layer_fetch(&t, 7, out, 256);
    for (int i = 0; i < 256; ++i) assert(out[i] == 1);

    /* logical_line=8: row 1 -> tile 2 */
    crt_tile_layer_fetch(&t, 8, out, 256);
    for (int i = 0; i < 256; ++i) assert(out[i] == 2);

    /* scroll_y=1: logical_line=7 becomes screen_y=8 -> tile 2 */
    crt_tile_set_scroll(&t, 0, 1);
    crt_tile_layer_fetch(&t, 7, out, 256);
    for (int i = 0; i < 256; ++i) assert(out[i] == 2);
    printf("  fetch with scroll_y crosses tile: OK\n");
}

static void test_scroll_wraparound(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64 * 4];
    uint8_t nt[32 * 32];
    memset(nt, 0, sizeof(nt));
    fill_constant_patterns(pattern, 4);
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 4, nt);

    /* Only column 0 painted with tile 1 */
    crt_tile_set_tile(&t, 0, 0, 1);

    /* scroll_x that lands visible pixel 248 at column 0 (248..255 = tile 1,
     * then wrap to column 0 again = tile 1 at 0..7?) Actually with 32
     * visible tiles (logical_w=256), scroll_x=248 means tile column
     * (248 >> 3) = 31, fine = 0. Column 31 is tile 0. Column 0 wraps
     * past end, also 0. Tile 1 at column 0 only shows when scroll_x
     * crosses back to column 0 in view. */
    crt_tile_set_scroll(&t, 248, 0);
    uint8_t out[256];
    crt_tile_layer_fetch(&t, 0, out, 256);
    /* Columns 31, 0, 1, 2, ..., 30 are displayed. Column 0 is tile 1.
     * That column appears at pixels 8..15 (after column 31 at 0..7). */
    for (int i = 0; i < 8; ++i)  assert(out[i] == 0);
    for (int i = 8; i < 16; ++i) assert(out[i] == 1);
    for (int i = 16; i < 256; ++i) assert(out[i] == 0);
    printf("  scroll wraparound: OK\n");
}

static void test_fast_path_parity_with_fallback(void)
{
    /* Both paths must render identical logical content when scaled
     * to the same DAC width. The fast path fires at width=768 for
     * the 256 logical -> 768 exact 3:1 case; the fallback fires at
     * any other width. We verify expected transitions on both. */
    crt_tile_layer_t t;
    uint8_t pattern[64 * 4];
    uint8_t nt[32 * 32];
    memset(nt, 0, sizeof(nt));
    fill_constant_patterns(pattern, 4);
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 4, nt);

    /* Column c gets tile (c & 3). So logical pixel x ∈ [0,256) maps
     * to tile (x>>3) & 3. First tile boundary is at logical x=8 (0->1),
     * then x=16 (1->2), x=24 (2->3), x=32 (3->0), ... */
    for (uint16_t c = 0; c < 32; ++c) {
        crt_tile_set_tile(&t, c, 0, (uint8_t)(c & 3));
    }

    /* Fast path: width 768 -> exact 3:1. DAC[0..23] = 0, [24..47] = 1,
     * [48..71] = 2, [72..95] = 3, [96..119] = 0, ... */
    uint8_t buf_fast[768];
    assert(crt_tile_layer_fetch(&t, 0, buf_fast, 768) == true);
    for (int i = 0;  i < 24;  ++i) assert(buf_fast[i] == 0);
    for (int i = 24; i < 48;  ++i) assert(buf_fast[i] == 1);
    for (int i = 48; i < 72;  ++i) assert(buf_fast[i] == 2);
    for (int i = 72; i < 96;  ++i) assert(buf_fast[i] == 3);

    /* Fallback: width 512 -> exact 2:1. DAC[0..15] = 0, [16..31] = 1,
     * [32..47] = 2, [48..63] = 3. */
    uint8_t buf_fallback[512];
    assert(crt_tile_layer_fetch(&t, 0, buf_fallback, 512) == true);
    for (int i = 0;  i < 16;  ++i) assert(buf_fallback[i] == 0);
    for (int i = 16; i < 32;  ++i) assert(buf_fallback[i] == 1);
    for (int i = 32; i < 48;  ++i) assert(buf_fallback[i] == 2);
    for (int i = 48; i < 64;  ++i) assert(buf_fallback[i] == 3);

    printf("  fast path + fallback render identical content: OK\n");
}

static void test_scanline_hook_parity_with_fetch(void)
{
    /* For the same layer + palette, the fused hook output (writes
     * active_buf directly) must equal the fetch output fed through
     * palette LUT + I2S word-swap — i.e. what compose would produce
     * in the generic path. */
    crt_tile_layer_t t;
    uint8_t pattern[64 * 4];
    uint8_t nt[32 * 32];
    memset(nt, 0, sizeof(nt));
    fill_constant_patterns(pattern, 4);
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 4, nt);
    init_linear_palette();
    crt_tile_set_palette(&t, g_palette);

    /* Paint a pattern */
    for (uint16_t c = 0; c < 32; ++c) {
        crt_tile_set_tile(&t, c, 2, (uint8_t)(c & 3));
    }

    crt_scanline_t sc = make_active_line(17);  /* row 2, fine_y=1 */

    /* Fused hook path */
    uint16_t buf_hook[768];
    memset(buf_hook, 0, sizeof(buf_hook));
    crt_tile_scanline_hook(&sc, buf_hook, 768, &t);

    /* fetch + palette+swap (what compose generic path does) */
    uint8_t line[256];  /* logical */
    crt_tile_layer_fetch(&t, 17, line, 256);
    uint16_t buf_fetch[768];
    memset(buf_fetch, 0, sizeof(buf_fetch));
    /* Replicate compose palette+swap using the tile fast-path triplet
     * layout: each logical pixel becomes 3 equal samples, then the
     * I2S swap flips every pair. */
    for (int p = 0; p < 128; ++p) {
        uint16_t l0 = g_palette[line[p * 2]];
        uint16_t l1 = g_palette[line[p * 2 + 1]];
        buf_fetch[p * 6    ] = l0;
        buf_fetch[p * 6 + 1] = l0;
        buf_fetch[p * 6 + 2] = l1;
        buf_fetch[p * 6 + 3] = l0;
        buf_fetch[p * 6 + 4] = l1;
        buf_fetch[p * 6 + 5] = l1;
    }

    for (int i = 0; i < 768; ++i) {
        assert(buf_hook[i] == buf_fetch[i]);
    }
    printf("  scanline hook parity with fetch path: OK\n");
}

static void test_missing_palette_noop(void)
{
    crt_tile_layer_t t;
    uint8_t pattern[64] = {0};
    uint8_t nt[32 * 32] = {0};
    crt_tile_init(&t, 32, 30, 32, 32, pattern, 1, nt);
    /* No palette set */

    uint16_t buf[8] = { 0xBEEF, 0xBEEF, 0xBEEF, 0xBEEF,
                        0xBEEF, 0xBEEF, 0xBEEF, 0xBEEF };
    crt_scanline_t sc = make_active_line(0);
    crt_tile_scanline_hook(&sc, buf, 8, &t);

    for (int i = 0; i < 8; ++i) assert(buf[i] == 0xBEEF);
    printf("  missing palette is no-op: OK\n");
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void)
{
    printf("crt_tile test\n");
    test_init_validation();
    test_set_get_tile_roundtrip();
    test_scroll_normalization();
    test_fetch_static_nametable();
    test_fetch_with_scroll_x();
    test_fetch_with_scroll_y_crosses_tile();
    test_scroll_wraparound();
    test_fast_path_parity_with_fallback();
    test_scanline_hook_parity_with_fetch();
    test_missing_palette_noop();
    printf("ALL PASSED\n");
    return 0;
}
