#include "crt_tile.h"

#include "esp_attr.h"
#include "esp_check.h"

#include <string.h>

#define CRT_TILE_STACK_LOGICAL_W 256U

/* ── Internal helpers ─────────────────────────────────────────────── */

/* True when v is a positive power of two. */
static inline bool is_pow2(uint16_t v)
{
    return v != 0u && (v & (uint16_t)(v - 1u)) == 0u;
}

/* Wrap x into [0, mod). mod must be > 0. For power-of-two mod the
 * caller should use `& (mod - 1)` in the hot path instead. */
static inline uint16_t wrap_u16(int v, int mod)
{
    int r = v % mod;
    if (r < 0)
        r += mod;
    return (uint16_t)r;
}

/* ── Lifecycle ────────────────────────────────────────────────────── */

esp_err_t crt_tile_init(crt_tile_layer_t *t, uint16_t visible_w, uint16_t visible_h,
                        uint16_t pitch_w, uint16_t pitch_h, const uint8_t *pattern_table,
                        uint16_t pattern_count, uint8_t *nametable)
{
    ESP_RETURN_ON_FALSE(t != NULL, ESP_ERR_INVALID_ARG, "crt_tile", "null state");
    ESP_RETURN_ON_FALSE(visible_w > 0 && visible_h > 0, ESP_ERR_INVALID_ARG, "crt_tile",
                        "zero visible dim");
    ESP_RETURN_ON_FALSE(pitch_w >= visible_w && pitch_h >= visible_h, ESP_ERR_INVALID_ARG,
                        "crt_tile", "pitch smaller than visible");
    ESP_RETURN_ON_FALSE(pattern_table != NULL && pattern_count > 0, ESP_ERR_INVALID_ARG, "crt_tile",
                        "null pattern table");
    ESP_RETURN_ON_FALSE(pattern_count <= 256, ESP_ERR_INVALID_ARG, "crt_tile",
                        "pattern_count > 256");
    ESP_RETURN_ON_FALSE(nametable != NULL, ESP_ERR_INVALID_ARG, "crt_tile", "null nametable");

    *t = (crt_tile_layer_t){
        .visible_w_tiles = visible_w,
        .visible_h_tiles = visible_h,
        .pitch_w_tiles = pitch_w,
        .pitch_h_tiles = pitch_h,
        .pitch_w_mask = is_pow2(pitch_w) ? (uint16_t)(pitch_w - 1u) : 0u,
        .pitch_h_mask = is_pow2(pitch_h) ? (uint16_t)(pitch_h - 1u) : 0u,
        .pattern_table = pattern_table,
        .pattern_count = pattern_count,
        .nametable = nametable,
        .scroll_x_px = 0,
        .scroll_y_px = 0,
        .palette = NULL,
    };
    return ESP_OK;
}

/* ── Mutation ─────────────────────────────────────────────────────── */

void crt_tile_set_tile(crt_tile_layer_t *t, uint16_t col, uint16_t row, uint8_t tile_idx)
{
    if (t == NULL || t->nametable == NULL)
        return;
    if (col >= t->pitch_w_tiles || row >= t->pitch_h_tiles)
        return;
    t->nametable[(size_t)row * t->pitch_w_tiles + col] = tile_idx;
}

uint8_t crt_tile_get_tile(const crt_tile_layer_t *t, uint16_t col, uint16_t row)
{
    if (t == NULL || t->nametable == NULL)
        return 0;
    if (col >= t->pitch_w_tiles || row >= t->pitch_h_tiles)
        return 0;
    return t->nametable[(size_t)row * t->pitch_w_tiles + col];
}

void crt_tile_set_scroll(crt_tile_layer_t *t, int x_px, int y_px)
{
    if (t == NULL)
        return;
    const int vw_px = (int)t->visible_w_tiles * (int)CRT_TILE_PX_W;
    const int vh_px = (int)t->visible_h_tiles * (int)CRT_TILE_PX_H;
    t->scroll_x_px = wrap_u16(x_px, vw_px);
    t->scroll_y_px = wrap_u16(y_px, vh_px);
}

void crt_tile_set_palette(crt_tile_layer_t *t, const uint16_t *palette)
{
    if (t == NULL)
        return;
    t->palette = palette;
}

/* ── Hot path building blocks ─────────────────────────────────────── */

/* Compose one logical scanline (visible_w_tiles * 8 pixels) into @p out.
 * Caller owns the buffer. This works for both the fast path (direct
 * expansion consumer) and the generic fallback (fixed-point sampler). */
static IRAM_ATTR void tile_render_logical_line(const crt_tile_layer_t *t, uint16_t y,
                                               uint8_t *logical_out)
{
    const uint16_t logical_w_px = (uint16_t)(t->visible_w_tiles * CRT_TILE_PX_W);
    const uint16_t logical_h_px = (uint16_t)(t->visible_h_tiles * CRT_TILE_PX_H);

    uint16_t screen_y = (uint16_t)(y + t->scroll_y_px);
    if (t->pitch_h_mask != 0u && logical_h_px == (uint16_t)(t->pitch_h_tiles * CRT_TILE_PX_H)) {
        /* pitch_h_tiles is PoT AND visible matches pitch on Y: AND wrap */
        screen_y = (uint16_t)(screen_y & (uint16_t)((t->pitch_h_tiles * CRT_TILE_PX_H) - 1u));
    } else {
        if (screen_y >= logical_h_px) {
            screen_y = (uint16_t)(screen_y % logical_h_px);
        }
    }
    const uint16_t tile_row_full = (uint16_t)(screen_y >> 3);
    const uint16_t fine_y = (uint16_t)(screen_y & 7u);
    const uint16_t tile_row = (t->pitch_h_mask != 0u)
                                  ? (uint16_t)(tile_row_full & t->pitch_h_mask)
                                  : (uint16_t)(tile_row_full % t->pitch_h_tiles);

    const uint8_t *nametable_row = &t->nametable[(size_t)tile_row * t->pitch_w_tiles];
    const uint8_t *pattern = t->pattern_table;
    const uint16_t pattern_count = t->pattern_count;

    /* Walk tile columns, emit 8 logical pixels per tile. */
    uint16_t scroll_tile_col = (uint16_t)(t->scroll_x_px >> 3);
    uint16_t scroll_fine_x = (uint16_t)(t->scroll_x_px & 7u);
    uint8_t *dst = logical_out;
    uint16_t remaining = logical_w_px;

    uint16_t col = scroll_tile_col;
    uint16_t fine = scroll_fine_x;

    while (remaining > 0u) {
        const uint16_t wrapped_col = (t->pitch_w_mask != 0u) ? (uint16_t)(col & t->pitch_w_mask)
                                                             : (uint16_t)(col % t->pitch_w_tiles);
        uint8_t idx = nametable_row[wrapped_col];
        if (idx >= pattern_count)
            idx = 0;
        const uint8_t *tile_line =
            &pattern[(size_t)idx * CRT_TILE_BYTES + (size_t)fine_y * CRT_TILE_PX_W];
        uint16_t take = (uint16_t)(CRT_TILE_PX_W - fine);
        if (take > remaining)
            take = remaining;
        for (uint16_t i = 0; i < take; ++i) {
            dst[i] = tile_line[fine + i];
        }
        dst += take;
        remaining = (uint16_t)(remaining - take);
        fine = 0;
        col++;
    }
}

/* ── Compose layer adapter ────────────────────────────────────────── */

IRAM_ATTR bool crt_tile_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out,
                                    uint16_t width)
{
    crt_tile_layer_t *t = (crt_tile_layer_t *)ctx;
    if (t == NULL || idx_out == NULL || width == 0u)
        return false;

    const uint16_t visible_h_px = (uint16_t)(t->visible_h_tiles * CRT_TILE_PX_H);
    if (logical_line >= visible_h_px) {
        memset(idx_out, 0, width);
        return true;
    }

    const uint16_t logical_w_px = (uint16_t)(t->visible_w_tiles * CRT_TILE_PX_W);

    /* Render one logical scanline into a stack buffer; max visible
     * width is 256 pixels for the common 32-tile case. Capped at
     * CRT_TILE_MAX_LOGICAL_W to bound stack usage on exotic configs. */
    uint8_t logical_line_buf[CRT_TILE_STACK_LOGICAL_W];
    if (logical_w_px > CRT_TILE_STACK_LOGICAL_W) {
        memset(idx_out, 0, width);
        return true;
    }
    tile_render_logical_line(t, logical_line, logical_line_buf);

    /* Fast path: exact 3:1 X expansion. 256 logical -> 768 DAC samples.
     * No fixed-point arithmetic in the inner loop. */
    if (logical_w_px == 256u && width == 768u) {
        uint8_t *dst = idx_out;
        for (uint16_t i = 0; i < 256u; ++i) {
            uint8_t v = logical_line_buf[i];
            dst[0] = v;
            dst[1] = v;
            dst[2] = v;
            dst += 3;
        }
        return true;
    }

    /* Generic fallback: fixed-point nearest-neighbor with CEILING
     * rounding in the step so integer-multiple expansions (e.g. 3:1)
     * collapse to the exact replication produced by the fast path. */
    uint32_t step = (((uint32_t)logical_w_px << 16) + (uint32_t)width - 1u) / (uint32_t)width;
    uint32_t acc = 0;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = logical_line_buf[acc >> 16];
        acc += step;
    }
    return true;
}

/* ── Fused scanline hook ──────────────────────────────────────────── */

IRAM_ATTR void crt_tile_scanline_hook(const crt_scanline_t *scanline, uint16_t *active_buf,
                                      uint16_t active_width, void *user_data)
{
    const crt_tile_layer_t *t = (const crt_tile_layer_t *)user_data;
    if (scanline == NULL || t == NULL || active_buf == NULL || active_width == 0u ||
        t->palette == NULL || !CRT_SCANLINE_HAS_LOGICAL(scanline)) {
        return;
    }

    const uint16_t visible_h_px = (uint16_t)(t->visible_h_tiles * CRT_TILE_PX_H);
    if (scanline->logical_line >= visible_h_px) {
        return;
    }

    const uint16_t logical_w_px = (uint16_t)(t->visible_w_tiles * CRT_TILE_PX_W);

    uint8_t logical_line_buf[CRT_TILE_STACK_LOGICAL_W];
    if (logical_w_px > CRT_TILE_STACK_LOGICAL_W) {
        return;
    }
    tile_render_logical_line(t, scanline->logical_line, logical_line_buf);

    const uint16_t *pal = t->palette;

    /* Fast path: 256 logical -> 768 DAC with palette + I2S word-swap
     * fused into a single pass. Each logical pixel expands to 3 samples;
     * each pair of consecutive logical pixels (l0, l1) produces 6 output
     * samples split across 3 DAC pairs (pre-swap layout, then swapped):
     *
     *   pair (6p+0, 6p+1) = (l0, l0) -> swap = (l0, l0)
     *   pair (6p+2, 6p+3) = (l0, l1) -> swap = (l1, l0)
     *   pair (6p+4, 6p+5) = (l1, l1) -> swap = (l1, l1)
     *
     * 128 logical-pixel-pairs x 6 samples = 768 outputs. */
    if (logical_w_px == 256u && active_width == 768u) {
        for (uint16_t p = 0; p < 128u; ++p) {
            uint16_t l0 = pal[logical_line_buf[(uint16_t)(p * 2u)]];
            uint16_t l1 = pal[logical_line_buf[(uint16_t)(p * 2u + 1u)]];
            const uint16_t base = (uint16_t)(p * 6u);
            active_buf[base] = l0;
            active_buf[base + 1] = l0;
            active_buf[base + 2] = l1;
            active_buf[base + 3] = l0;
            active_buf[base + 4] = l1;
            active_buf[base + 5] = l1;
        }
        return;
    }

    /* Generic fallback: ceiling-step fixed-point + palette + swap.
     * Ceiling step aligns with the fast-path 3:1 replication so both
     * paths produce bit-identical output for matching dimensions. */
    uint32_t step =
        (((uint32_t)logical_w_px << 16) + (uint32_t)active_width - 1u) / (uint32_t)active_width;
    uint32_t acc = 0;
    const uint16_t even_width = active_width & (uint16_t)~1U;
    uint16_t i = 0;
    for (; i < even_width; i += 2) {
        uint16_t p0 = pal[logical_line_buf[acc >> 16]];
        acc += step;
        uint16_t p1 = pal[logical_line_buf[acc >> 16]];
        acc += step;
        active_buf[i] = p1;
        active_buf[i + 1] = p0;
    }
    if (i < active_width) {
        active_buf[i] = pal[logical_line_buf[acc >> 16]];
    }
}
