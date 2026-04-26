#pragma once

#include "crt_timing_types.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t crt_line_policy_sync_width(const crt_timing_profile_t *timing,
                                  crt_timing_line_type_t line_type);
bool crt_line_policy_has_burst(crt_timing_line_type_t line_type);
void crt_line_policy_apply_sync(const crt_timing_profile_t *timing, uint16_t line_index,
                                crt_timing_line_type_t line_type, uint16_t *samples,
                                size_t sample_count, uint16_t sync_level);

#ifdef __cplusplus
}
#endif
