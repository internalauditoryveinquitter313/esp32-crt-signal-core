#pragma once

#include "crt_timing_types.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t crt_timing_get_profile(crt_video_standard_t standard, crt_timing_profile_t *out_profile);
crt_timing_line_type_t crt_timing_get_line_type(crt_video_standard_t standard, uint16_t line_index);
crt_timing_line_type_t crt_timing_get_profile_line_type(const crt_timing_profile_t *profile,
                                                        uint16_t line_index);

bool crt_timing_get_active_line_index(const crt_timing_profile_t *profile, uint16_t line_index,
                                      uint16_t *out_active_line_index);

uint16_t crt_timing_get_first_blank_line_after_active(const crt_timing_profile_t *profile);

#ifdef __cplusplus
}
#endif
