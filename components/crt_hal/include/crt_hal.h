#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate_hz;
    size_t dma_line_count;
    size_t dma_samples_per_line;
    int video_gpio_num;
} crt_hal_config_t;

esp_err_t crt_hal_init(const crt_hal_config_t *config);
esp_err_t crt_hal_start(void);
esp_err_t crt_hal_stop(void);
esp_err_t crt_hal_shutdown(void);
size_t crt_hal_get_slot_count(void);
esp_err_t crt_hal_get_line_buffer(size_t slot_index, uint16_t **out_buffer);
esp_err_t crt_hal_wait_recycled_slot(size_t *out_slot_index, uint32_t ticks_to_wait);
uint32_t crt_hal_get_dma_underrun_count(void);
size_t crt_hal_get_recycled_queue_depth(void);

#ifdef __cplusplus
}
#endif
