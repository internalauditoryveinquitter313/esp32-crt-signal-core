#include "crt_compose_layers.h"

#include <string.h>

void crt_compose_solid_layer_init(crt_compose_solid_layer_t *layer, uint8_t fill_idx)
{
    if (layer != NULL) {
        layer->fill_idx = fill_idx;
    }
}

bool crt_compose_solid_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out,
                                   uint16_t width)
{
    (void)logical_line;
    const crt_compose_solid_layer_t *layer = (const crt_compose_solid_layer_t *)ctx;
    if (layer == NULL || idx_out == NULL || width == 0) {
        return false;
    }

    memset(idx_out, layer->fill_idx, width);
    return true;
}

void crt_compose_rect_layer_init(crt_compose_rect_layer_t *layer, uint16_t x, uint16_t y,
                                 uint16_t width, uint16_t height, uint8_t fill_idx,
                                 uint8_t transparent_idx)
{
    if (layer == NULL) {
        return;
    }

    layer->x = x;
    layer->y = y;
    layer->width = width;
    layer->height = height;
    layer->fill_idx = fill_idx;
    layer->transparent_idx = transparent_idx;
}

void crt_compose_rect_layer_set_bounds(crt_compose_rect_layer_t *layer, uint16_t x, uint16_t y,
                                       uint16_t width, uint16_t height)
{
    if (layer == NULL) {
        return;
    }

    layer->x = x;
    layer->y = y;
    layer->width = width;
    layer->height = height;
}

void crt_compose_rect_layer_set_fill(crt_compose_rect_layer_t *layer, uint8_t fill_idx)
{
    if (layer != NULL) {
        layer->fill_idx = fill_idx;
    }
}

bool crt_compose_rect_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out,
                                  uint16_t width)
{
    const crt_compose_rect_layer_t *layer = (const crt_compose_rect_layer_t *)ctx;
    if (layer == NULL || idx_out == NULL || width == 0 || layer->width == 0 || layer->height == 0) {
        return false;
    }

    const uint32_t y0 = layer->y;
    const uint32_t y1 = y0 + layer->height;
    if (logical_line < y0 || logical_line >= y1 || layer->x >= width) {
        return false;
    }

    memset(idx_out, layer->transparent_idx, width);

    const uint32_t x0 = layer->x;
    uint32_t x1 = x0 + layer->width;
    if (x1 > width) {
        x1 = width;
    }

    memset(&idx_out[x0], layer->fill_idx, x1 - x0);
    return true;
}

void crt_compose_checker_layer_init(crt_compose_checker_layer_t *layer, uint8_t first_idx,
                                    uint8_t second_idx, uint8_t cell_w, uint8_t cell_h)
{
    if (layer == NULL) {
        return;
    }

    layer->first_idx = first_idx;
    layer->second_idx = second_idx;
    layer->cell_w = (cell_w == 0) ? 1 : cell_w;
    layer->cell_h = (cell_h == 0) ? 1 : cell_h;
}

bool crt_compose_checker_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out,
                                     uint16_t width)
{
    const crt_compose_checker_layer_t *layer = (const crt_compose_checker_layer_t *)ctx;
    if (layer == NULL || idx_out == NULL || width == 0) {
        return false;
    }

    const uint16_t row = logical_line / layer->cell_h;
    for (uint16_t x = 0; x < width; ++x) {
        const uint16_t col = x / layer->cell_w;
        idx_out[x] = (((row + col) & 0x1U) == 0) ? layer->first_idx : layer->second_idx;
    }
    return true;
}

static uint16_t crt_compose_wrap_i32(int32_t value, uint16_t limit)
{
    int32_t wrapped = value % (int32_t)limit;
    if (wrapped < 0) {
        wrapped += limit;
    }
    return (uint16_t)wrapped;
}

void crt_compose_viewport_layer_init(crt_compose_viewport_layer_t *layer,
                                     crt_layer_fetch_fn source_fetch, void *source_ctx,
                                     uint16_t source_width, uint16_t source_height,
                                     uint8_t transparent_idx)
{
    if (layer == NULL) {
        return;
    }

    memset(layer, 0, sizeof(*layer));
    layer->source_fetch = source_fetch;
    layer->source_ctx = source_ctx;
    layer->source_width = source_width;
    layer->source_height = source_height;
    layer->viewport_width = source_width;
    layer->viewport_height = source_height;
    layer->transparent_idx = transparent_idx;
}

void crt_compose_viewport_layer_set_source(crt_compose_viewport_layer_t *layer,
                                           crt_layer_fetch_fn source_fetch, void *source_ctx,
                                           uint16_t source_width, uint16_t source_height)
{
    if (layer == NULL) {
        return;
    }

    layer->source_fetch = source_fetch;
    layer->source_ctx = source_ctx;
    layer->source_width = source_width;
    layer->source_height = source_height;
}

void crt_compose_viewport_layer_set_viewport(crt_compose_viewport_layer_t *layer, uint16_t x,
                                             uint16_t y, uint16_t width, uint16_t height)
{
    if (layer == NULL) {
        return;
    }

    layer->viewport_x = x;
    layer->viewport_y = y;
    layer->viewport_width = width;
    layer->viewport_height = height;
}

void crt_compose_viewport_layer_set_scroll(crt_compose_viewport_layer_t *layer, int32_t x,
                                           int32_t y)
{
    if (layer == NULL) {
        return;
    }

    layer->scroll_x = x;
    layer->scroll_y = y;
}

void crt_compose_viewport_layer_scroll_by(crt_compose_viewport_layer_t *layer, int32_t dx,
                                          int32_t dy)
{
    if (layer == NULL) {
        return;
    }

    layer->scroll_x += dx;
    layer->scroll_y += dy;
}

bool crt_compose_viewport_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out,
                                      uint16_t width)
{
    crt_compose_viewport_layer_t *layer = (crt_compose_viewport_layer_t *)ctx;
    if (layer == NULL || idx_out == NULL || width == 0 || layer->source_fetch == NULL ||
        layer->source_width == 0 || layer->source_height == 0 ||
        layer->source_width > CRT_COMPOSE_MAX_WIDTH || layer->viewport_width == 0 ||
        layer->viewport_height == 0) {
        return false;
    }

    const uint32_t y0 = layer->viewport_y;
    const uint32_t y1 = y0 + layer->viewport_height;
    if (logical_line < y0 || logical_line >= y1 || layer->viewport_x >= width) {
        return false;
    }

    const uint16_t rel_y = (uint16_t)(logical_line - layer->viewport_y);
    const uint16_t source_y =
        crt_compose_wrap_i32((int32_t)rel_y + layer->scroll_y, layer->source_height);

    uint32_t out_count = layer->viewport_width;
    if ((uint32_t)layer->viewport_x + out_count > width) {
        out_count = width - layer->viewport_x;
    }

    /* Clear only the regions outside the viewport rect. Two partial memsets
     * are strictly cheaper than one full-line memset on the common case
     * where the viewport covers a meaningful slice of the active line. */
    memset(idx_out, layer->transparent_idx, layer->viewport_x);
    const uint32_t end_x = (uint32_t)layer->viewport_x + out_count;
    if (end_x < width) {
        memset(&idx_out[end_x], layer->transparent_idx, width - end_x);
    }

    /* Fast path: no horizontal scroll and the source width matches the
     * visible viewport. The source can write straight into idx_out at the
     * viewport offset, skipping the internal scratch buffer entirely. */
    if (layer->scroll_x == 0 && (uint32_t)layer->source_width == out_count) {
        if (!layer->source_fetch(layer->source_ctx, source_y, &idx_out[layer->viewport_x],
                                 (uint16_t)out_count)) {
            memset(&idx_out[layer->viewport_x], layer->transparent_idx, out_count);
        }
        return true;
    }

    /* Generic path: scaling/scrolling case still uses internal scratch. */
    if (!layer->source_fetch(layer->source_ctx, source_y, layer->scratch, layer->source_width)) {
        memset(&idx_out[layer->viewport_x], layer->transparent_idx, out_count);
        return true;
    }

    for (uint32_t i = 0; i < out_count; ++i) {
        const uint16_t source_x =
            crt_compose_wrap_i32((int32_t)i + layer->scroll_x, layer->source_width);
        idx_out[layer->viewport_x + i] = layer->scratch[source_x];
    }
    return true;
}
