#include "crt_compose.h"

#include "esp_attr.h"
#include "esp_check.h"

#include <string.h>

/* ── Lifecycle ────────────────────────────────────────────────────── */

esp_err_t crt_compose_init(crt_compose_t *c)
{
    ESP_RETURN_ON_FALSE(c != NULL, ESP_ERR_INVALID_ARG, "crt_compose", "null state");
    memset(c, 0, sizeof(*c));
    return ESP_OK;
}

esp_err_t crt_compose_set_palette(crt_compose_t *c, const uint16_t *palette, uint16_t size)
{
    ESP_RETURN_ON_FALSE(c != NULL, ESP_ERR_INVALID_ARG, "crt_compose", "null state");
    ESP_RETURN_ON_FALSE(palette == NULL || size >= 256U, ESP_ERR_INVALID_SIZE, "crt_compose",
                        "indexed-8 palette requires 256 entries");
    c->palette = palette;
    c->palette_size = (palette != NULL) ? size : 0;
    return ESP_OK;
}

void crt_compose_set_clear_index(crt_compose_t *c, uint8_t idx)
{
    if (c != NULL) {
        c->clear_idx = idx;
    }
}

/* ── Layer management ─────────────────────────────────────────────── */

static esp_err_t crt_compose_append_layer(crt_compose_t *c, crt_layer_fetch_fn fetch,
                                          crt_scanline_hook_fn scanline_override, void *ctx,
                                          uint16_t transparent_idx, uint8_t *out_layer_idx)
{
    ESP_RETURN_ON_FALSE(c != NULL, ESP_ERR_INVALID_ARG, "crt_compose", "null state");
    ESP_RETURN_ON_FALSE(fetch != NULL, ESP_ERR_INVALID_ARG, "crt_compose", "null fetch");
    ESP_RETURN_ON_FALSE(c->layer_count < CRT_COMPOSE_MAX_LAYERS, ESP_ERR_NO_MEM, "crt_compose",
                        "layer stack full");

    const uint8_t layer_idx = c->layer_count;
    c->layers[c->layer_count] = (crt_compose_layer_t){
        .fetch = fetch,
        .scanline_override = scanline_override,
        .ctx = ctx,
        .transparent_idx = transparent_idx,
        .enabled = true,
    };
    c->layer_count++;
    if (out_layer_idx != NULL) {
        *out_layer_idx = layer_idx;
    }
    return ESP_OK;
}

esp_err_t crt_compose_add_layer(crt_compose_t *c, crt_layer_fetch_fn fetch, void *ctx,
                                uint16_t transparent_idx)
{
    return crt_compose_append_layer(c, fetch, NULL, ctx, transparent_idx, NULL);
}

esp_err_t crt_compose_add_layer_with_id(crt_compose_t *c, crt_layer_fetch_fn fetch, void *ctx,
                                        uint16_t transparent_idx, uint8_t *out_layer_idx)
{
    if (out_layer_idx != NULL) {
        *out_layer_idx = CRT_COMPOSE_LAYER_INVALID;
    }
    return crt_compose_append_layer(c, fetch, NULL, ctx, transparent_idx, out_layer_idx);
}

esp_err_t crt_compose_add_layer_fused(crt_compose_t *c, crt_layer_fetch_fn fetch,
                                      crt_scanline_hook_fn scanline_override, void *ctx)
{
    return crt_compose_append_layer(c, fetch, scanline_override, ctx, CRT_COMPOSE_NO_TRANSPARENCY,
                                    NULL);
}

esp_err_t crt_compose_add_layer_fused_with_id(crt_compose_t *c, crt_layer_fetch_fn fetch,
                                              crt_scanline_hook_fn scanline_override, void *ctx,
                                              uint8_t *out_layer_idx)
{
    if (out_layer_idx != NULL) {
        *out_layer_idx = CRT_COMPOSE_LAYER_INVALID;
    }
    return crt_compose_append_layer(c, fetch, scanline_override, ctx, CRT_COMPOSE_NO_TRANSPARENCY,
                                    out_layer_idx);
}

void crt_compose_clear_layers(crt_compose_t *c)
{
    if (c != NULL) {
        c->layer_count = 0;
    }
}

void crt_compose_set_layer_enabled(crt_compose_t *c, uint8_t layer_idx, bool enabled)
{
    if (c != NULL && layer_idx < c->layer_count) {
        c->layers[layer_idx].enabled = enabled;
    }
}

static bool crt_compose_layer_idx_valid(const crt_compose_t *c, uint8_t layer_idx)
{
    return c != NULL && layer_idx < c->layer_count;
}

esp_err_t crt_compose_get_layer_info(const crt_compose_t *c, uint8_t layer_idx,
                                     crt_compose_layer_info_t *out_info)
{
    ESP_RETURN_ON_FALSE(crt_compose_layer_idx_valid(c, layer_idx), ESP_ERR_INVALID_ARG,
                        "crt_compose", "invalid layer");
    ESP_RETURN_ON_FALSE(out_info != NULL, ESP_ERR_INVALID_ARG, "crt_compose", "null out_info");

    const crt_compose_layer_t *layer = &c->layers[layer_idx];
    *out_info = (crt_compose_layer_info_t){
        .fetch = layer->fetch,
        .scanline_override = layer->scanline_override,
        .ctx = layer->ctx,
        .transparent_idx = layer->transparent_idx,
        .enabled = layer->enabled,
    };
    return ESP_OK;
}

esp_err_t crt_compose_set_layer_fetch(crt_compose_t *c, uint8_t layer_idx, crt_layer_fetch_fn fetch,
                                      void *ctx)
{
    ESP_RETURN_ON_FALSE(crt_compose_layer_idx_valid(c, layer_idx), ESP_ERR_INVALID_ARG,
                        "crt_compose", "invalid layer");
    ESP_RETURN_ON_FALSE(fetch != NULL, ESP_ERR_INVALID_ARG, "crt_compose", "null fetch");

    c->layers[layer_idx].fetch = fetch;
    c->layers[layer_idx].ctx = ctx;
    return ESP_OK;
}

esp_err_t crt_compose_set_layer_context(crt_compose_t *c, uint8_t layer_idx, void *ctx)
{
    ESP_RETURN_ON_FALSE(crt_compose_layer_idx_valid(c, layer_idx), ESP_ERR_INVALID_ARG,
                        "crt_compose", "invalid layer");

    c->layers[layer_idx].ctx = ctx;
    return ESP_OK;
}

esp_err_t crt_compose_set_layer_transparent_index(crt_compose_t *c, uint8_t layer_idx,
                                                  uint16_t transparent_idx)
{
    ESP_RETURN_ON_FALSE(crt_compose_layer_idx_valid(c, layer_idx), ESP_ERR_INVALID_ARG,
                        "crt_compose", "invalid layer");

    c->layers[layer_idx].transparent_idx = transparent_idx;
    return ESP_OK;
}

esp_err_t crt_compose_swap_layers(crt_compose_t *c, uint8_t first_layer_idx,
                                  uint8_t second_layer_idx)
{
    ESP_RETURN_ON_FALSE(crt_compose_layer_idx_valid(c, first_layer_idx), ESP_ERR_INVALID_ARG,
                        "crt_compose", "invalid first layer");
    ESP_RETURN_ON_FALSE(crt_compose_layer_idx_valid(c, second_layer_idx), ESP_ERR_INVALID_ARG,
                        "crt_compose", "invalid second layer");

    if (first_layer_idx == second_layer_idx) {
        return ESP_OK;
    }

    crt_compose_layer_t tmp = c->layers[first_layer_idx];
    c->layers[first_layer_idx] = c->layers[second_layer_idx];
    c->layers[second_layer_idx] = tmp;
    return ESP_OK;
}

/* ── Scanline hook ────────────────────────────────────────────────── */

IRAM_ATTR void crt_compose_scanline_hook(const crt_scanline_t *scanline, uint16_t *active_buf,
                                         uint16_t active_width, void *user_data)
{
    crt_compose_t *c = (crt_compose_t *)user_data;

    if (scanline == NULL || c == NULL || active_buf == NULL || active_width == 0 ||
        active_width > CRT_COMPOSE_MAX_WIDTH || c->palette == NULL ||
        !CRT_SCANLINE_HAS_LOGICAL(scanline)) {
        return;
    }

    /* Pre-scan: classify the active layer stack for the hot path picker.
     * Two layer counts matter:
     *   opaque_count — layers with transparent_idx == NO_TRANSPARENCY
     *   keyed_count  — layers with keyed transparency
     * The fused base is the unique opaque layer when one exists and carries
     * a scanline_override. */
    int base_idx = -1;
    int keyed_idx = -1;
    uint8_t opaque_count = 0;
    uint8_t keyed_count = 0;
    for (uint8_t li = 0; li < c->layer_count; ++li) {
        const crt_compose_layer_t *layer = &c->layers[li];
        if (!layer->enabled || layer->fetch == NULL)
            continue;
        if (layer->transparent_idx == CRT_COMPOSE_NO_TRANSPARENCY) {
            base_idx = li;
            opaque_count++;
        } else {
            keyed_idx = li;
            keyed_count++;
        }
    }
    const bool base_fused_eligible =
        (opaque_count == 1) && (c->layers[base_idx].scanline_override != NULL);

    /* Fused hot path for the common PPU-style case: one opaque base with
     * scanline_override + exactly one keyed overlay. Collapses
     * fetch + merge + palette + swap into two passes instead of three. */
    if (base_fused_eligible && keyed_count == 1) {
        const crt_compose_layer_t *base = &c->layers[base_idx];
        const crt_compose_layer_t *keyed = &c->layers[keyed_idx];

        if (!keyed->fetch(keyed->ctx, scanline->logical_line, c->scratch, active_width)) {
            base->scanline_override(scanline, active_buf, active_width, base->ctx);
            return;
        }

        (void)base->fetch(base->ctx, scanline->logical_line, c->line, active_width);

        const uint8_t key = (uint8_t)keyed->transparent_idx;
        const uint16_t *pal = c->palette;
        const uint16_t even_width = active_width & (uint16_t)~1U;
        uint16_t i = 0;
        for (; i < even_width; i += 2) {
            uint8_t s0 = c->scratch[i];
            uint8_t s1 = c->scratch[i + 1];
            uint8_t v0 = (s0 != key) ? s0 : c->line[i];
            uint8_t v1 = (s1 != key) ? s1 : c->line[i + 1];
            active_buf[i] = pal[v1];
            active_buf[i + 1] = pal[v0];
        }
        if (i < active_width) {
            uint8_t s = c->scratch[i];
            uint8_t v = (s != key) ? s : c->line[i];
            active_buf[i] = pal[v];
        }
        return;
    }

    if (base_fused_eligible) {
        /* Lazy materialization path.
         *
         * base.fetch is deferred until a keyed layer actually contributes.
         * If none does, we delegate the whole scanline to base.scanline_override
         * — bit-exact with the pre-compose hook, zero materialization cost.
         * If at least one keyed contributes, we materialize the base into
         * c->line, merge scratches into it, and finish with palette+swap.
         */
        const crt_compose_layer_t *base = &c->layers[base_idx];
        bool line_materialized = false;

        for (uint8_t li = 0; li < c->layer_count; ++li) {
            if ((int)li == base_idx)
                continue;
            const crt_compose_layer_t *layer = &c->layers[li];
            if (!layer->enabled || layer->fetch == NULL)
                continue;

            if (!layer->fetch(layer->ctx, scanline->logical_line, c->scratch, active_width)) {
                continue;
            }

            if (!line_materialized) {
                (void)base->fetch(base->ctx, scanline->logical_line, c->line, active_width);
                line_materialized = true;
            }

            const uint8_t key = (uint8_t)layer->transparent_idx;
            for (uint16_t x = 0; x < active_width; ++x) {
                uint8_t s = c->scratch[x];
                if (s != key) {
                    c->line[x] = s;
                }
            }
        }

        if (!line_materialized) {
            base->scanline_override(scanline, active_buf, active_width, base->ctx);
            return;
        }
        /* else: fall through to the palette + word-swap pass below. */
    } else {
        /* Generic path (no fused base): composite layers back-to-front.
         *  - Opaque layer writes directly into c->line, skipping memcpy.
         *    When the first enabled layer is opaque we skip the base clear.
         *  - Keyed layer returning false is skipped with no merge work.
         */
        bool line_ready = false;
        for (uint8_t li = 0; li < c->layer_count; ++li) {
            const crt_compose_layer_t *layer = &c->layers[li];
            if (!layer->enabled || layer->fetch == NULL)
                continue;

            if (layer->transparent_idx == CRT_COMPOSE_NO_TRANSPARENCY) {
                (void)layer->fetch(layer->ctx, scanline->logical_line, c->line, active_width);
                line_ready = true;
                continue;
            }

            if (!line_ready) {
                memset(c->line, c->clear_idx, active_width);
                line_ready = true;
            }

            if (!layer->fetch(layer->ctx, scanline->logical_line, c->scratch, active_width)) {
                continue;
            }

            const uint8_t key = (uint8_t)layer->transparent_idx;
            for (uint16_t x = 0; x < active_width; ++x) {
                uint8_t s = c->scratch[x];
                if (s != key) {
                    c->line[x] = s;
                }
            }
        }

        if (!line_ready) {
            memset(c->line, c->clear_idx, active_width);
        }
    }

    /* Final pass: palette LUT + I2S word-swap (paired 16-bit writes).
     * Mirrors crt_fb_scanline_hook to keep the hot path identical. */
    const uint8_t *line = c->line;
    const uint16_t *pal = c->palette;
    const uint16_t even_width = active_width & (uint16_t)~1U;
    uint16_t i = 0;

    for (; i < even_width; i += 2) {
        uint16_t p0 = pal[line[i]];
        uint16_t p1 = pal[line[i + 1]];
        active_buf[i] = p1;
        active_buf[i + 1] = p0;
    }
    if (i < active_width) {
        active_buf[i] = pal[line[i]];
    }
}
