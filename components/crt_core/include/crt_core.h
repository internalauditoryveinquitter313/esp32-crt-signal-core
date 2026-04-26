#pragma once

#include "crt_demo_pattern.h"
#include "crt_diag.h"
#include "crt_timing.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    crt_video_standard_t video_standard;
    bool enable_color;
    crt_demo_pattern_mode_t demo_pattern_mode;
    uint16_t target_ready_depth;
    uint16_t min_ready_depth;
    int prep_task_core;
} crt_core_config_t;

esp_err_t crt_core_init(const crt_core_config_t *config);
esp_err_t crt_core_start(void);
esp_err_t crt_core_stop(void);
esp_err_t crt_core_deinit(void);
esp_err_t crt_core_get_diag_snapshot(crt_diag_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
