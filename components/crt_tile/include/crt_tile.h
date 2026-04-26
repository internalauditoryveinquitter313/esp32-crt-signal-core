#ifndef CRT_TILE_HEADER_H
#define CRT_TILE_HEADER_H

#include "crt_scanline.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file crt_tile.h
 * @brief Tilemap backend for crt_compose (PPU-style BG layer).
 *
 * An indexed-8 tilemap that plugs into crt_compose as an opaque base
 * layer. Pattern table is a const array of 8x8 tiles (64 bytes each).
 * Nametable is a mutable grid of tile indices; each byte selects one
 * tile from the pattern table.
 *
 * Dimensional model:
 *   visible_w/h_tiles — rendered region (e.g. 32x30 -> 256x240 logical)
 *   pitch_w/h_tiles   — nametable stride (power-of-two enables fast
 *                       wraparound via bitwise AND; recommended 32x32)
 *
 * Fast path is taken when:
 *   - visible_w_tiles * 8 == 256 and active_width == 768 (exact 3:1 X
 *     expansion, 1:1 Y — zero fixed-point in the hot loop)
 *   - pitch_w_tiles / pitch_h_tiles are powers of two (mask wraparound)
 *
 * Fallback handles arbitrary dimensions via fixed-point scale with
 * modulo wraparound. Parity between the two paths is tested.
 *
 * Integration:
 *     crt_tile_layer_t tile;
 *     crt_tile_init(&tile, 32, 30, 32, 32, pattern, 256, nametable);
 *     crt_compose_add_layer_fused(&compose, crt_tile_layer_fetch,
 *                                 crt_tile_scanline_hook, &tile);
 */

#define CRT_TILE_PX_W  8u
#define CRT_TILE_PX_H  8u
#define CRT_TILE_BYTES ((size_t)(CRT_TILE_PX_W * CRT_TILE_PX_H))

typedef struct {
    /* Public dimensions. All tile counts, not pixels. */
    uint16_t visible_w_tiles;
    uint16_t visible_h_tiles;
    uint16_t pitch_w_tiles;
    uint16_t pitch_h_tiles;

    /* Derived mask state. Set only when the corresponding pitch is a
     * power of two; enables AND-based wraparound in the hot path.
     * When zero, the layer falls back to modulo wraparound. */
    uint16_t pitch_w_mask;
    uint16_t pitch_h_mask;

    /* Pattern/nametable storage. Pattern is const (caller owns memory,
     * typically DRAM_ATTR internal RAM). Nametable is mutable and
     * holds pitch_w_tiles * pitch_h_tiles bytes. */
    const uint8_t *pattern_table;
    uint16_t pattern_count;
    uint8_t *nametable;

    /* Scroll in pixel units, always stored normalized inside
     * [0, visible_w_tiles * 8) and [0, visible_h_tiles * 8). */
    uint16_t scroll_x_px;
    uint16_t scroll_y_px;

    /* Palette (256-entry DAC LUT, uint16_t) used by the fused
     * scanline hook only. Must match the compose palette so
     * delegation is bit-exact with the generic compose output. */
    const uint16_t *palette;
} crt_tile_layer_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

/**
 * @brief Initialise a tile layer descriptor.
 *
 * @param t              Layer to initialise (caller-allocated).
 * @param visible_w      Tile columns rendered (must be > 0).
 * @param visible_h      Tile rows rendered (must be > 0).
 * @param pitch_w        Nametable column stride (>= visible_w).
 * @param pitch_h        Nametable row count (>= visible_h).
 * @param pattern_table  Pattern table pointer, pattern_count * 64 bytes.
 * @param pattern_count  Number of tiles (<= 256).
 * @param nametable      Nametable buffer, pitch_w * pitch_h bytes.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG otherwise.
 */
esp_err_t crt_tile_init(crt_tile_layer_t *t, uint16_t visible_w, uint16_t visible_h,
                        uint16_t pitch_w, uint16_t pitch_h, const uint8_t *pattern_table,
                        uint16_t pattern_count, uint8_t *nametable);

/* ── Mutation ─────────────────────────────────────────────────────── */

void crt_tile_set_tile(crt_tile_layer_t *t, uint16_t col, uint16_t row, uint8_t tile_idx);
uint8_t crt_tile_get_tile(const crt_tile_layer_t *t, uint16_t col, uint16_t row);

/**
 * @brief Set scroll in pixel units. Signed input accepted; the layer
 *        normalises internally to the visible region so the hot path
 *        never sees negative values or unbounded modulo.
 */
void crt_tile_set_scroll(crt_tile_layer_t *t, int x_px, int y_px);

/* ── Scanline contract ────────────────────────────────────────────── */

/**
 * @brief crt_compose layer fetch adapter. Always returns true (opaque
 *        base); out-of-range rows are zero-filled as a soft guard.
 */
bool crt_tile_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width);

/**
 * @brief Fused scanline hook mirroring crt_fb_scanline_hook. Writes
 *        DAC samples directly into active_buf with palette LUT + I2S
 *        word-swap baked in. Register with crt_compose_add_layer_fused
 *        so compose can delegate directly on lines where no keyed
 *        overlay contributes.
 */
void crt_tile_scanline_hook(const crt_scanline_t *scanline, uint16_t *active_buf,
                            uint16_t active_width, void *user_data);

/**
 * @brief Palette used by crt_tile_scanline_hook. Must be set to the
 *        same 256-entry DAC LUT as the compose state so delegation is
 *        bit-exact with the generic compose output pass.
 */
void crt_tile_set_palette(crt_tile_layer_t *t, const uint16_t *palette);

#ifdef __cplusplus
}
#endif

#endif /* CRT_TILE_HEADER_H */
