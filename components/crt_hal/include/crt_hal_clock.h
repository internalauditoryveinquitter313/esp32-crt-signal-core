#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint32_t sample_rate_hz;
    uint32_t apll_hz;
    uint32_t o_div;
    uint32_t sdm0;
    uint32_t sdm1;
    uint32_t sdm2;
} crt_hal_apll_coeff_t;

bool crt_hal_apll_coeffs_for_sample_rate(uint32_t sample_rate_hz, crt_hal_apll_coeff_t *out_coeffs);

#ifdef __cplusplus
}
#endif
