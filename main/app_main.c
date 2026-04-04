#include <stdbool.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "crt_core.h"
#include "crt_fb.h"

static const char *TAG = "app_main";
static const bool k_enable_color = CONFIG_CRT_ENABLE_COLOR;
static crt_fb_surface_t s_fb;

#define APP_FB_WIDTH    256
#define APP_BLANK_LEVEL ((uint16_t)(23U << 8))
#define APP_WHITE_LEVEL ((uint16_t)(0xFFU << 8))

static esp_err_t app_start_core(crt_video_standard_t video_standard)
{
    crt_core_config_t config = {
        .video_standard = video_standard,
        .enable_color = k_enable_color,
        .demo_pattern_mode = k_enable_color ? CRT_DEMO_PATTERN_COLOR_BARS_RAMP : CRT_DEMO_PATTERN_LUMA_BARS,
        .target_ready_depth = 8,
        .min_ready_depth = 4,
        .prep_task_core = 1,
    };

    esp_err_t err = crt_core_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "crt_core_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* ── Framebuffer: Liskov substitute for demo pattern ──────────── */
    {
        crt_timing_profile_t timing;
        crt_timing_get_profile(video_standard, &timing);

        err = crt_fb_surface_init(&s_fb, APP_FB_WIDTH, timing.active_lines, CRT_FB_FORMAT_INDEXED8);
        if (err == ESP_OK) { err = crt_fb_surface_alloc(&s_fb); }
        if (err == ESP_OK) {
            crt_fb_palette_init_grayscale(&s_fb, APP_BLANK_LEVEL, APP_WHITE_LEVEL);

            /* Calibration pattern: step response + line pairs + ramp */
            for (uint16_t y = 0; y < s_fb.height; ++y) {
                uint8_t *row = crt_fb_row(&s_fb, y);
                if (y < s_fb.height / 3) {
                    /* Top third: sharp step transitions at different levels */
                    /* |0|255|0|192|0|128|0|64| — measures overshoot per amplitude */
                    uint16_t section = s_fb.width / 8;
                    for (uint16_t x = 0; x < s_fb.width; ++x) {
                        uint8_t col = (uint8_t)(x / section);
                        uint8_t levels[] = {0, 255, 0, 192, 0, 128, 0, 64};
                        row[x] = levels[col < 8 ? col : 7];
                    }
                } else if (y < (s_fb.height * 2) / 3) {
                    /* Middle third: line pairs with decreasing spacing */
                    /* Tests effective resolution: 32px, 16px, 8px, 4px, 2px, 1px */
                    uint16_t zone = s_fb.width / 6;
                    uint16_t spacings[] = {32, 16, 8, 4, 2, 1};
                    for (uint16_t x = 0; x < s_fb.width; ++x) {
                        uint8_t z = (uint8_t)(x / zone);
                        if (z >= 6) z = 5;
                        uint16_t sp = spacings[z];
                        uint16_t local_x = x - z * zone;
                        row[x] = ((local_x / sp) & 1) ? 255 : 0;
                    }
                } else {
                    /* Bottom third: continuous 0→255 ramp — measures gamma */
                    for (uint16_t x = 0; x < s_fb.width; ++x) {
                        row[x] = (uint8_t)((uint32_t)x * 255 / (s_fb.width - 1));
                    }
                }
            }

            crt_register_scanline_hook(crt_fb_scanline_hook, &s_fb);
            ESP_LOGI(TAG, "framebuffer: %ux%u indexed8 (%u bytes), scanline hook active",
                     s_fb.width, s_fb.height, (unsigned)s_fb.buffer_size);
        } else {
            ESP_LOGW(TAG, "framebuffer alloc failed, falling back to demo pattern");
        }
    }

    err = crt_core_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "crt_core_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG,
             "ESP32 CRT signal core started: standard=%s color=%s pattern=%s",
             (video_standard == CRT_VIDEO_STANDARD_PAL) ? "PAL" : "NTSC",
             k_enable_color ? "on" : "off",
             (config.demo_pattern_mode == CRT_DEMO_PATTERN_COLOR_BARS_RAMP) ? "color_bars_ramp" : "luma_bars");

    return ESP_OK;
}

void app_main(void)
{
    crt_video_standard_t video_standard =
#if CONFIG_CRT_VIDEO_STANDARD_PAL
        CRT_VIDEO_STANDARD_PAL;
#else
        CRT_VIDEO_STANDARD_NTSC;
#endif

    if (app_start_core(video_standard) != ESP_OK) {
        return;
    }

#if CONFIG_CRT_TEST_STANDARD_TOGGLE
    while (true) {
        esp_err_t err;

        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_CRT_TEST_STANDARD_TOGGLE_INTERVAL_S * 1000U));
        video_standard = (video_standard == CRT_VIDEO_STANDARD_PAL) ? CRT_VIDEO_STANDARD_NTSC : CRT_VIDEO_STANDARD_PAL;

        ESP_LOGI(TAG,
                 "test toggle: switching standard to %s (interval=%" PRIu32 "s)",
                 (video_standard == CRT_VIDEO_STANDARD_PAL) ? "PAL" : "NTSC",
                 (uint32_t)CONFIG_CRT_TEST_STANDARD_TOGGLE_INTERVAL_S);

        err = crt_core_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "crt_core_stop failed during toggle: %s", esp_err_to_name(err));
            break;
        }
        err = crt_core_deinit();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "crt_core_deinit failed during toggle: %s", esp_err_to_name(err));
            break;
        }
        if (app_start_core(video_standard) != ESP_OK) {
            break;
        }
    }
#endif
}
