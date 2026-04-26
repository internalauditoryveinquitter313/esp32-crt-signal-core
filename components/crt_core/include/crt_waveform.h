#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crt_waveform_fill_ntsc_burst_template(uint16_t *samples, size_t sample_count,
                                           uint16_t blank_level);
void crt_waveform_fill_pal_burst_template(uint16_t *samples, size_t sample_count,
                                          uint16_t blank_level, bool invert_phase);

#ifdef __cplusplus
}
#endif
