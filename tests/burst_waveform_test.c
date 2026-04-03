#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "crt_waveform.h"

static void test_ntsc_burst_template(void)
{
    uint16_t samples[8] = {0};
    const uint16_t expected[8] = {150, 100, 50, 100, 150, 100, 50, 100};

    crt_waveform_fill_ntsc_burst_template(samples, 8, 100);
    assert(memcmp(samples, expected, sizeof(expected)) == 0);
}

static void test_pal_even_burst_template(void)
{
    uint16_t samples[8] = {0};
    const uint16_t expected[8] = {53, 147, 147, 53, 53, 147, 147, 53};

    crt_waveform_fill_pal_burst_template(samples, 8, 100, false);
    assert(memcmp(samples, expected, sizeof(expected)) == 0);
}

static void test_pal_odd_burst_template(void)
{
    uint16_t samples[8] = {0};
    const uint16_t expected[8] = {147, 147, 53, 53, 147, 147, 53, 53};

    crt_waveform_fill_pal_burst_template(samples, 8, 100, true);
    assert(memcmp(samples, expected, sizeof(expected)) == 0);
}

int main(void)
{
    test_ntsc_burst_template();
    test_pal_even_burst_template();
    test_pal_odd_burst_template();
    return 0;
}
