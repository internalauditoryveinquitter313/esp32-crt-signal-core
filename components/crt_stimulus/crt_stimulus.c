#include "crt_stimulus.h"

#include <string.h>

#define CRT_STIMULUS_DEFAULT_HEIGHT 240U
#define CRT_STIMULUS_DEFAULT_SEED   0x43525453u

static uint8_t scale_u8(uint32_t value, uint32_t max_value)
{
    if (max_value == 0) {
        return 0;
    }
    return (uint8_t)((value * 255U) / max_value);
}

static uint32_t stimulus_hash(uint32_t x, uint32_t y, uint32_t frame, uint32_t seed)
{
    uint32_t h = seed ^ (x * 0x9E3779B1u) ^ (y * 0x85EBCA77u) ^ (frame * 0xC2B2AE3Du);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

void crt_stimulus_default_config(crt_stimulus_config_t *config)
{
    if (config == NULL) {
        return;
    }

    *config = (crt_stimulus_config_t){
        .height = CRT_STIMULUS_DEFAULT_HEIGHT,
        .pattern = CRT_STIMULUS_PATTERN_FRAME_MARKERS,
        .low_idx = 0,
        .high_idx = 255,
        .mid_idx = 96,
        .cell_w = 16,
        .cell_h = 8,
        .impulse_x = CRT_STIMULUS_AUTO,
        .impulse_y = CRT_STIMULUS_AUTO,
        .seed = CRT_STIMULUS_DEFAULT_SEED,
    };
}

esp_err_t crt_stimulus_init(crt_stimulus_t *stimulus, const crt_stimulus_config_t *config)
{
    if (stimulus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    crt_stimulus_config_t local_config;
    if (config == NULL) {
        crt_stimulus_default_config(&local_config);
        config = &local_config;
    }
    if (config->height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    stimulus->config = *config;
    stimulus->config.cell_w = (stimulus->config.cell_w == 0) ? 1 : stimulus->config.cell_w;
    stimulus->config.cell_h = (stimulus->config.cell_h == 0) ? 1 : stimulus->config.cell_h;
    stimulus->frame = 0;
    return ESP_OK;
}

void crt_stimulus_set_pattern(crt_stimulus_t *stimulus, crt_stimulus_pattern_t pattern)
{
    if (stimulus != NULL) {
        stimulus->config.pattern = pattern;
    }
}

void crt_stimulus_set_frame(crt_stimulus_t *stimulus, uint32_t frame)
{
    if (stimulus != NULL) {
        stimulus->frame = frame;
    }
}

void crt_stimulus_advance_frame(crt_stimulus_t *stimulus)
{
    if (stimulus != NULL) {
        stimulus->frame++;
    }
}

static void fill_horizontal_ramp(uint8_t *idx_out, uint16_t width)
{
    const uint32_t max_x = (uint32_t)width - 1U;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = scale_u8(x, max_x);
    }
}

static void fill_vertical_ramp(const crt_stimulus_t *stimulus, uint16_t logical_line,
                               uint8_t *idx_out, uint16_t width)
{
    const uint32_t max_y = (uint32_t)stimulus->config.height - 1U;
    memset(idx_out, scale_u8(logical_line, max_y), width);
}

static void fill_checker(const crt_stimulus_t *stimulus, uint16_t logical_line, uint8_t *idx_out,
                         uint16_t width)
{
    const uint16_t row = logical_line / stimulus->config.cell_h;
    for (uint16_t x = 0; x < width; ++x) {
        const uint16_t col = x / stimulus->config.cell_w;
        idx_out[x] =
            (((row + col) & 0x1U) == 0) ? stimulus->config.low_idx : stimulus->config.high_idx;
    }
}

static void fill_prbs(const crt_stimulus_t *stimulus, uint16_t logical_line, uint8_t *idx_out,
                      uint16_t width)
{
    const uint32_t frame = stimulus->frame;
    const uint32_t seed = stimulus->config.seed;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = (stimulus_hash(x, logical_line, frame, seed) & 0x80000000u)
                         ? stimulus->config.high_idx
                         : stimulus->config.low_idx;
    }
}

static void fill_impulse(const crt_stimulus_t *stimulus, uint16_t logical_line, uint8_t *idx_out,
                         uint16_t width)
{
    memset(idx_out, stimulus->config.low_idx, width);

    uint16_t impulse_x = stimulus->config.impulse_x;
    uint16_t impulse_y = stimulus->config.impulse_y;
    if (impulse_x == CRT_STIMULUS_AUTO) {
        impulse_x = (uint16_t)((stimulus->frame * 7U) % width);
    }
    if (impulse_y == CRT_STIMULUS_AUTO) {
        impulse_y = (uint16_t)((stimulus->frame * 3U) % stimulus->config.height);
    }

    if (logical_line == impulse_y && impulse_x < width) {
        idx_out[impulse_x] = stimulus->config.high_idx;
    }
}

static void fill_chirp(const crt_stimulus_t *stimulus, uint16_t logical_line, uint8_t *idx_out,
                       uint16_t width)
{
    const uint32_t max_y = (uint32_t)stimulus->config.height - 1U;
    uint16_t period = (uint16_t)(2U + ((uint32_t)logical_line * 62U / (max_y ? max_y : 1U)));
    if (period < 2U) {
        period = 2U;
    }
    const uint16_t phase = (uint16_t)(stimulus->frame % period);
    const uint16_t duty = period / 2U;

    for (uint16_t x = 0; x < width; ++x) {
        uint16_t pos = (uint16_t)((x + phase) % period);
        idx_out[x] = (pos < duty) ? stimulus->config.high_idx : stimulus->config.low_idx;
    }
}

static void fill_frame_markers(const crt_stimulus_t *stimulus, uint16_t logical_line,
                               uint8_t *idx_out, uint16_t width)
{
    memset(idx_out, stimulus->config.mid_idx, width);

    if (logical_line < 8U) {
        const uint32_t bit = (stimulus->frame >> logical_line) & 0x1U;
        const uint8_t edge_idx = bit ? stimulus->config.high_idx : stimulus->config.low_idx;
        memset(idx_out, edge_idx, width);
        for (uint16_t x = 0; x < width; x += 32U) {
            idx_out[x] = stimulus->config.high_idx;
        }
        return;
    }

    if (stimulus->config.height > 8U && logical_line >= stimulus->config.height - 8U) {
        for (uint16_t x = 0; x < width; ++x) {
            idx_out[x] = ((x / 16U) & 0x1U) ? stimulus->config.high_idx : stimulus->config.low_idx;
        }
        return;
    }

    const uint32_t max_x = (uint32_t)width - 1U;
    const uint32_t max_y = (uint32_t)stimulus->config.height - 1U;
    for (uint16_t x = 0; x < width; ++x) {
        idx_out[x] = (uint8_t)((scale_u8(x, max_x) + scale_u8(logical_line, max_y)) >> 1);
    }
}

bool crt_stimulus_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width)
{
    const crt_stimulus_t *stimulus = (const crt_stimulus_t *)ctx;
    if (stimulus == NULL || idx_out == NULL || width == 0 ||
        logical_line >= stimulus->config.height) {
        return false;
    }

    switch (stimulus->config.pattern) {
    case CRT_STIMULUS_PATTERN_HORIZONTAL_RAMP:
        fill_horizontal_ramp(idx_out, width);
        return true;
    case CRT_STIMULUS_PATTERN_VERTICAL_RAMP:
        fill_vertical_ramp(stimulus, logical_line, idx_out, width);
        return true;
    case CRT_STIMULUS_PATTERN_CHECKER:
        fill_checker(stimulus, logical_line, idx_out, width);
        return true;
    case CRT_STIMULUS_PATTERN_PRBS:
        fill_prbs(stimulus, logical_line, idx_out, width);
        return true;
    case CRT_STIMULUS_PATTERN_IMPULSE:
        fill_impulse(stimulus, logical_line, idx_out, width);
        return true;
    case CRT_STIMULUS_PATTERN_CHIRP:
        fill_chirp(stimulus, logical_line, idx_out, width);
        return true;
    case CRT_STIMULUS_PATTERN_FRAME_MARKERS:
        fill_frame_markers(stimulus, logical_line, idx_out, width);
        return true;
    default:
        memset(idx_out, stimulus->config.low_idx, width);
        return true;
    }
}
