#include "crt_fb.h"

#include "crt_composite_palette.h"

#include "esp_attr.h"
#include "esp_check.h"

#include <stdlib.h>
#include <string.h>

#define CRT_FB_PALETTE_SIZE_INDEXED8 256

/* ── Lifecycle ────────────────────────────────────────────────────── */

esp_err_t crt_fb_surface_init(crt_fb_surface_t *surface, uint16_t width, uint16_t height,
                              crt_fb_format_t format)
{
    ESP_RETURN_ON_FALSE(surface != NULL, ESP_ERR_INVALID_ARG, "crt_fb", "surface is null");
    ESP_RETURN_ON_FALSE(width > 0 && height > 0, ESP_ERR_INVALID_ARG, "crt_fb", "zero dimension");

    *surface = (crt_fb_surface_t){
        .width = width,
        .height = height,
        .format = format,
        .buffer = NULL,
        .buffer_size = 0,
        .palette = NULL,
        .palette_size = 0,
    };
    return ESP_OK;
}

esp_err_t crt_fb_surface_alloc(crt_fb_surface_t *surface)
{
    size_t buf_size;
    uint16_t palette_entries;

    ESP_RETURN_ON_FALSE(surface != NULL, ESP_ERR_INVALID_ARG, "crt_fb", "surface is null");
    ESP_RETURN_ON_FALSE(surface->buffer == NULL, ESP_ERR_INVALID_STATE, "crt_fb",
                        "already allocated");

    switch (surface->format) {
    case CRT_FB_FORMAT_INDEXED8:
        buf_size = (size_t)surface->width * surface->height;
        palette_entries = CRT_FB_PALETTE_SIZE_INDEXED8;
        break;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }

    surface->buffer = calloc(1, buf_size);
    ESP_RETURN_ON_FALSE(surface->buffer != NULL, ESP_ERR_NO_MEM, "crt_fb",
                        "buffer alloc failed (%u bytes)", (unsigned)buf_size);
    surface->buffer_size = buf_size;

    surface->palette = calloc(palette_entries, sizeof(uint16_t));
    if (surface->palette == NULL) {
        free(surface->buffer);
        surface->buffer = NULL;
        surface->buffer_size = 0;
        return ESP_ERR_NO_MEM;
    }
    surface->palette_size = palette_entries;

    return ESP_OK;
}

esp_err_t crt_fb_surface_free(crt_fb_surface_t *surface)
{
    ESP_RETURN_ON_FALSE(surface != NULL, ESP_ERR_INVALID_ARG, "crt_fb", "surface is null");

    free(surface->buffer);
    surface->buffer = NULL;
    surface->buffer_size = 0;
    free(surface->palette);
    surface->palette = NULL;
    surface->palette_size = 0;

    return ESP_OK;
}

esp_err_t crt_fb_surface_deinit(crt_fb_surface_t *surface)
{
    ESP_RETURN_ON_FALSE(surface != NULL, ESP_ERR_INVALID_ARG, "crt_fb", "surface is null");

    if (surface->buffer != NULL) {
        crt_fb_surface_free(surface);
    }
    memset(surface, 0, sizeof(*surface));
    return ESP_OK;
}

/* ── Pixel access ─────────────────────────────────────────────────── */

uint8_t *crt_fb_row(const crt_fb_surface_t *surface, uint16_t y)
{
    if (surface == NULL || surface->buffer == NULL || y >= surface->height) {
        return NULL;
    }
    return &surface->buffer[(size_t)y * surface->width];
}

void crt_fb_put(crt_fb_surface_t *surface, uint16_t x, uint16_t y, uint8_t value)
{
    if (surface == NULL || surface->buffer == NULL)
        return;
    if (x < surface->width && y < surface->height) {
        surface->buffer[(size_t)y * surface->width + x] = value;
    }
}

uint8_t crt_fb_get(const crt_fb_surface_t *surface, uint16_t x, uint16_t y)
{
    if (surface == NULL || surface->buffer == NULL)
        return 0;
    if (x < surface->width && y < surface->height) {
        return surface->buffer[(size_t)y * surface->width + x];
    }
    return 0;
}

void crt_fb_clear(crt_fb_surface_t *surface, uint8_t value)
{
    if (surface != NULL && surface->buffer != NULL) {
        memset(surface->buffer, value, surface->buffer_size);
    }
}

/* ── Palette ──────────────────────────────────────────────────────── */

void crt_fb_palette_set(crt_fb_surface_t *surface, uint8_t index, uint16_t dac_level)
{
    if (surface != NULL && surface->palette != NULL && index < surface->palette_size) {
        surface->palette[index] = dac_level;
    }
}

void crt_fb_palette_init_grayscale(crt_fb_surface_t *surface, uint16_t blank_level,
                                   uint16_t white_level)
{
    if (surface == NULL || surface->palette == NULL || surface->palette_size < 2) {
        return;
    }
    for (uint16_t i = 0; i < surface->palette_size; ++i) {
        surface->palette[i] = (uint16_t)(blank_level + (uint32_t)i * (white_level - blank_level) /
                                                           (surface->palette_size - 1));
    }
}

/* ── Scanline hook ────────────────────────────────────────────────── */

IRAM_ATTR void crt_fb_scanline_hook(const crt_scanline_t *scanline, uint16_t *active_buf,
                                    uint16_t active_width, void *user_data)
{
    const crt_fb_surface_t *surface = (const crt_fb_surface_t *)user_data;

    if (scanline == NULL || active_buf == NULL || active_width == 0 || surface == NULL ||
        !CRT_SCANLINE_HAS_LOGICAL(scanline) || scanline->logical_line >= surface->height ||
        surface->buffer == NULL || surface->palette == NULL) {
        return;
    }

    const uint8_t *row = &surface->buffer[(size_t)scanline->logical_line * surface->width];
    const uint16_t *pal = surface->palette;

    /* Fixed-point 16.16 stepping with I2S word-swap baked in.
     * Writes pairs of pixels in swapped order (index ^ 1) to avoid
     * the separate word-swap pass. ~3 cycles/pixel on Xtensa LX6. */
    uint32_t step = ((uint32_t)surface->width << 16) / active_width;
    uint32_t acc = 0;
    uint16_t i = 0;
    const uint16_t even_width = active_width & ~1U;

    for (; i < even_width; i += 2) {
        uint16_t p0 = pal[row[acc >> 16]];
        acc += step;
        uint16_t p1 = pal[row[acc >> 16]];
        acc += step;
        /* I2S expects swapped 16-bit words within each 32-bit DMA word */
        active_buf[i] = p1;
        active_buf[i + 1] = p0;
    }
    if (i < active_width) {
        active_buf[i] = pal[row[acc >> 16]];
    }
}

IRAM_ATTR void crt_fb_rgb332_scanline_hook(const crt_scanline_t *scanline, uint16_t *active_buf,
                                           uint16_t active_width, void *user_data)
{
    const crt_fb_surface_t *surface = (const crt_fb_surface_t *)user_data;
    uint8_t rgb332_row[CRT_COMPOSITE_RGB332_WIDTH];

    if (scanline == NULL || active_buf == NULL ||
        active_width != CRT_COMPOSITE_RGB332_ACTIVE_WIDTH || surface == NULL ||
        scanline->timing == NULL || !CRT_SCANLINE_HAS_LOGICAL(scanline) ||
        scanline->logical_line >= surface->height || surface->buffer == NULL ||
        surface->width == 0) {
        return;
    }

    const uint8_t *row = &surface->buffer[(size_t)scanline->logical_line * surface->width];
    if (surface->width == CRT_COMPOSITE_RGB332_WIDTH) {
        crt_composite_rgb332_render_256_to_768(scanline->timing->standard, scanline->physical_line,
                                               row, active_buf);
        return;
    }

    uint32_t step = ((uint32_t)surface->width << 16) / CRT_COMPOSITE_RGB332_WIDTH;
    uint32_t acc = 0;
    for (uint16_t x = 0; x < CRT_COMPOSITE_RGB332_WIDTH; ++x) {
        rgb332_row[x] = row[acc >> 16];
        acc += step;
    }
    crt_composite_rgb332_render_256_to_768(scanline->timing->standard, scanline->physical_line,
                                           rgb332_row, active_buf);
}

/* ── Compose layer adapter ────────────────────────────────────────── */

IRAM_ATTR bool crt_fb_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out,
                                  uint16_t width)
{
    const crt_fb_surface_t *surface = (const crt_fb_surface_t *)ctx;

    if (idx_out == NULL || width == 0) {
        return false;
    }
    if (surface == NULL || surface->buffer == NULL || logical_line >= surface->height ||
        surface->width == 0) {
        memset(idx_out, 0, width);
        return true;
    }

    const uint8_t *row = &surface->buffer[(size_t)logical_line * surface->width];
    if (width == surface->width) {
        memcpy(idx_out, row, width);
        return true;
    }
    uint32_t step = ((uint32_t)surface->width << 16) / width;
    uint32_t acc = 0;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = row[acc >> 16];
        acc += step;
    }
    return true;
}
