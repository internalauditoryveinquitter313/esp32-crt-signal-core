#include "crt_hal_clock.h"

#include <assert.h>

static void test_ntsc_apll_coeffs_match_esp_8_bit_reference(void)
{
    crt_hal_apll_coeff_t coeffs = {0};

    assert(crt_hal_apll_coeffs_for_sample_rate(14318180, &coeffs));
    assert(coeffs.sample_rate_hz == 14318180);
    assert(coeffs.apll_hz == 57272727);
    assert(coeffs.o_div == 1);
    assert(coeffs.sdm0 == 0x46);
    assert(coeffs.sdm1 == 0x97);
    assert(coeffs.sdm2 == 0x04);
}

static void test_pal_apll_coeffs_match_esp_8_bit_reference(void)
{
    crt_hal_apll_coeff_t coeffs = {0};

    assert(crt_hal_apll_coeffs_for_sample_rate(17734476, &coeffs));
    assert(coeffs.sample_rate_hz == 17734476);
    assert(coeffs.apll_hz == 70937904);
    assert(coeffs.o_div == 1);
    assert(coeffs.sdm0 == 0x04);
    assert(coeffs.sdm1 == 0xA4);
    assert(coeffs.sdm2 == 0x06);
}

static void test_unknown_sample_rate_is_rejected(void)
{
    crt_hal_apll_coeff_t coeffs = {
        .sample_rate_hz = 1,
        .apll_hz = 1,
        .o_div = 1,
        .sdm0 = 1,
        .sdm1 = 1,
        .sdm2 = 1,
    };

    assert(!crt_hal_apll_coeffs_for_sample_rate(12345678, &coeffs));
    assert(coeffs.sample_rate_hz == 0);
    assert(coeffs.apll_hz == 0);
}

int main(void)
{
    test_ntsc_apll_coeffs_match_esp_8_bit_reference();
    test_pal_apll_coeffs_match_esp_8_bit_reference();
    test_unknown_sample_rate_is_rejected();
    return 0;
}
