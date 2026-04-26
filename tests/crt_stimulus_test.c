#include "crt_stimulus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_default_config_and_init(void)
{
    crt_stimulus_config_t config;
    crt_stimulus_default_config(&config);

    assert(config.height == 240);
    assert(config.pattern == CRT_STIMULUS_PATTERN_FRAME_MARKERS);
    assert(config.cell_w > 0);
    assert(config.cell_h > 0);

    crt_stimulus_t stimulus;
    assert(crt_stimulus_init(&stimulus, &config) == ESP_OK);
    assert(stimulus.config.height == 240);
    assert(stimulus.frame == 0);

    config.height = 0;
    assert(crt_stimulus_init(&stimulus, &config) == ESP_ERR_INVALID_ARG);

    printf("  default config/init: OK\n");
}

static void test_horizontal_and_vertical_ramps(void)
{
    crt_stimulus_t stimulus;
    crt_stimulus_config_t config;
    crt_stimulus_default_config(&config);
    config.height = 4;
    config.pattern = CRT_STIMULUS_PATTERN_HORIZONTAL_RAMP;
    assert(crt_stimulus_init(&stimulus, &config) == ESP_OK);

    uint8_t out[4] = {0};
    assert(crt_stimulus_layer_fetch(&stimulus, 0, out, 4));
    assert(out[0] == 0);
    assert(out[1] == 85);
    assert(out[2] == 170);
    assert(out[3] == 255);

    crt_stimulus_set_pattern(&stimulus, CRT_STIMULUS_PATTERN_VERTICAL_RAMP);
    memset(out, 0, sizeof(out));
    assert(crt_stimulus_layer_fetch(&stimulus, 2, out, 4));
    assert(out[0] == 170);
    assert(out[1] == 170);
    assert(out[2] == 170);
    assert(out[3] == 170);

    printf("  ramps: OK\n");
}

static void test_checker_and_impulse(void)
{
    crt_stimulus_config_t config;
    crt_stimulus_default_config(&config);
    config.height = 8;
    config.pattern = CRT_STIMULUS_PATTERN_CHECKER;
    config.low_idx = 10;
    config.high_idx = 200;
    config.cell_w = 2;
    config.cell_h = 2;

    crt_stimulus_t stimulus;
    assert(crt_stimulus_init(&stimulus, &config) == ESP_OK);

    uint8_t out[8] = {0};
    assert(crt_stimulus_layer_fetch(&stimulus, 0, out, 8));
    assert(out[0] == 10 && out[1] == 10);
    assert(out[2] == 200 && out[3] == 200);

    config.pattern = CRT_STIMULUS_PATTERN_IMPULSE;
    config.impulse_x = 3;
    config.impulse_y = 5;
    assert(crt_stimulus_init(&stimulus, &config) == ESP_OK);
    memset(out, 0xAA, sizeof(out));
    assert(crt_stimulus_layer_fetch(&stimulus, 5, out, 8));
    for (uint8_t x = 0; x < 8; ++x) {
        assert(out[x] == ((x == 3) ? 200 : 10));
    }

    printf("  checker/impulse: OK\n");
}

static void test_prbs_is_deterministic_and_frame_dependent(void)
{
    crt_stimulus_config_t config;
    crt_stimulus_default_config(&config);
    config.pattern = CRT_STIMULUS_PATTERN_PRBS;
    config.height = 8;
    config.low_idx = 0;
    config.high_idx = 255;
    config.seed = 1234;

    crt_stimulus_t stimulus;
    assert(crt_stimulus_init(&stimulus, &config) == ESP_OK);

    uint8_t first[16] = {0};
    uint8_t second[16] = {0};
    uint8_t next_frame[16] = {0};

    crt_stimulus_set_frame(&stimulus, 7);
    assert(crt_stimulus_layer_fetch(&stimulus, 3, first, 16));
    assert(crt_stimulus_layer_fetch(&stimulus, 3, second, 16));
    assert(memcmp(first, second, sizeof(first)) == 0);

    crt_stimulus_set_frame(&stimulus, 8);
    assert(crt_stimulus_layer_fetch(&stimulus, 3, next_frame, 16));
    assert(memcmp(first, next_frame, sizeof(first)) != 0);

    printf("  PRBS determinism: OK\n");
}

static void test_frame_markers_encode_frame_bits(void)
{
    crt_stimulus_config_t config;
    crt_stimulus_default_config(&config);
    config.height = 16;
    config.pattern = CRT_STIMULUS_PATTERN_FRAME_MARKERS;
    config.low_idx = 1;
    config.high_idx = 250;
    config.mid_idx = 80;

    crt_stimulus_t stimulus;
    assert(crt_stimulus_init(&stimulus, &config) == ESP_OK);
    crt_stimulus_set_frame(&stimulus, 0x02);

    uint8_t out[8] = {0};
    assert(crt_stimulus_layer_fetch(&stimulus, 0, out, 8));
    assert(out[1] == 1);

    assert(crt_stimulus_layer_fetch(&stimulus, 1, out, 8));
    assert(out[1] == 250);

    assert(!crt_stimulus_layer_fetch(&stimulus, 16, out, 8));

    printf("  frame markers: OK\n");
}

int main(void)
{
    printf("crt_stimulus test\n");
    test_default_config_and_init();
    test_horizontal_and_vertical_ramps();
    test_checker_and_impulse();
    test_prbs_is_deterministic_and_frame_dependent();
    test_frame_markers_encode_frame_bits();
    printf("ALL PASSED\n");
    return 0;
}
