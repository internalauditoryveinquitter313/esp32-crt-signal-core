#ifndef CRT_STIMULUS_H
#define CRT_STIMULUS_H

#include "crt_compose.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRT_STIMULUS_AUTO ((uint16_t)0xFFFFu)

typedef enum {
    CRT_STIMULUS_PATTERN_HORIZONTAL_RAMP = 0,
    CRT_STIMULUS_PATTERN_VERTICAL_RAMP,
    CRT_STIMULUS_PATTERN_CHECKER,
    CRT_STIMULUS_PATTERN_PRBS,
    CRT_STIMULUS_PATTERN_IMPULSE,
    CRT_STIMULUS_PATTERN_CHIRP,
    CRT_STIMULUS_PATTERN_FRAME_MARKERS,
} crt_stimulus_pattern_t;

typedef struct {
    uint16_t height;
    crt_stimulus_pattern_t pattern;
    uint8_t low_idx;
    uint8_t high_idx;
    uint8_t mid_idx;
    uint8_t cell_w;
    uint8_t cell_h;
    uint16_t impulse_x;
    uint16_t impulse_y;
    uint32_t seed;
} crt_stimulus_config_t;

typedef struct {
    crt_stimulus_config_t config;
    uint32_t frame;
} crt_stimulus_t;

void crt_stimulus_default_config(crt_stimulus_config_t *config);
esp_err_t crt_stimulus_init(crt_stimulus_t *stimulus, const crt_stimulus_config_t *config);
void crt_stimulus_set_pattern(crt_stimulus_t *stimulus, crt_stimulus_pattern_t pattern);
void crt_stimulus_set_frame(crt_stimulus_t *stimulus, uint32_t frame);
void crt_stimulus_advance_frame(crt_stimulus_t *stimulus);

/**
 * @brief Indexed-8 compositor layer fetcher for deterministic measurement
 *        patterns.
 *
 * The layer emits active-video stimulus rows with no allocation, logging, or
 * peripheral access. It is intended to be registered as an opaque base layer
 * with `CRT_COMPOSE_NO_TRANSPARENCY`.
 */
bool crt_stimulus_layer_fetch(void *ctx, uint16_t logical_line, uint8_t *idx_out, uint16_t width);

#ifdef __cplusplus
}
#endif

#endif /* CRT_STIMULUS_H */
