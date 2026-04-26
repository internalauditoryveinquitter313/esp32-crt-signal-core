#include "crt_waveform.h"

#include <math.h>

void crt_waveform_fill_ntsc_burst_template(uint16_t *samples, size_t sample_count,
                                           uint16_t blank_level)
{
    const uint16_t amplitude = (uint16_t)(blank_level / 2U);

    for (size_t i = 0; i < sample_count; ++i) {
        switch (i & 0x3U) {
        case 0:
            samples[i] = (uint16_t)(blank_level + amplitude);
            break;
        case 2:
            samples[i] = (uint16_t)(blank_level - amplitude);
            break;
        default:
            samples[i] = blank_level;
            break;
        }
    }
}

void crt_waveform_fill_pal_burst_template(uint16_t *samples, size_t sample_count,
                                          uint16_t blank_level, bool invert_phase)
{
    const int32_t amplitude = (int32_t)lroundf((float)blank_level * 0.47140452f);
    static const int8_t k_phase_a[4] = {-1, 1, 1, -1};
    static const int8_t k_phase_b[4] = {1, 1, -1, -1};
    const int8_t *pattern = invert_phase ? k_phase_b : k_phase_a;

    for (size_t i = 0; i < sample_count; ++i) {
        samples[i] = (uint16_t)((int32_t)blank_level + (pattern[i & 0x3U] * amplitude));
    }
}
