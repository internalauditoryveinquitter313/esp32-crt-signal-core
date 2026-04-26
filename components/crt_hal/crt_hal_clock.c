#include "crt_hal_clock.h"

#include <stddef.h>

static const crt_hal_apll_coeff_t k_apll_profiles[] = {
    {
        .name = "NTSC 4x colorburst",
        .sample_rate_hz = 14318180,
        .apll_hz = 57272727,
        .o_div = 1,
        .sdm0 = 0x46,
        .sdm1 = 0x97,
        .sdm2 = 0x04,
    },
    {
        .name = "PAL 4x colorburst",
        .sample_rate_hz = 17734476,
        .apll_hz = 70937904,
        .o_div = 1,
        .sdm0 = 0x04,
        .sdm1 = 0xA4,
        .sdm2 = 0x06,
    },
};

bool crt_hal_apll_coeffs_for_sample_rate(uint32_t sample_rate_hz, crt_hal_apll_coeff_t *out_coeffs)
{
    if (out_coeffs == NULL) {
        return false;
    }

    for (size_t i = 0; i < sizeof(k_apll_profiles) / sizeof(k_apll_profiles[0]); ++i) {
        if (k_apll_profiles[i].sample_rate_hz == sample_rate_hz) {
            *out_coeffs = k_apll_profiles[i];
            return true;
        }
    }

    *out_coeffs = (crt_hal_apll_coeff_t){0};
    return false;
}
