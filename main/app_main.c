#include <stdbool.h>
#include <inttypes.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include "driver/uart.h"
#include "esp_vfs_dev.h"

#include "crt_core.h"
#include "crt_fb.h"
#include "godzilla_img.h"

static const char *TAG = "app_main";
static const bool k_enable_color = CONFIG_CRT_ENABLE_COLOR;
static crt_fb_surface_t s_fb;

#define APP_FB_WIDTH    256
#define APP_BLANK_LEVEL ((uint16_t)(23U << 8))
#define APP_WHITE_LEVEL ((uint16_t)(0x70U << 8))  /* ~44% DAC — tuned for C270 webcam capture */

/* ── Serial upload protocol ─────────────────────────────────────── */
#define UPLOAD_MAGIC     { 0xFB, 0xDA, 0x00, 0x01 }
#define UPLOAD_ACK       0x06
#define UPLOAD_NAK       0x15
#define UPLOAD_UART_NUM  UART_NUM_0
#define UPLOAD_RX_BUF    8192

/* ── Drawing primitives ──────────────────────────────────────────── */

static void fb_rect(crt_fb_surface_t *fb, int x, int y, int w, int h, uint8_t c) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            crt_fb_put(fb, (uint16_t) i, (uint16_t) j, c);
}

static void fb_ellipse(crt_fb_surface_t *fb, int cx, int cy, int rx, int ry, uint8_t c) {
    for (int j = cy - ry; j <= cy + ry; ++j)
        for (int i = cx - rx; i <= cx + rx; ++i) {
            int dx = i - cx, dy = j - cy;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
                crt_fb_put(fb, (uint16_t) i, (uint16_t) j, c);
        }
}

/* ── Serial upload: receive raw pixels via UART ─────────────────── */

static int s_uart_fd = -1;

static void uart_upload_check(crt_fb_surface_t *fb) {
    uint8_t byte;
    static const uint8_t magic[] = UPLOAD_MAGIC;

    if (s_uart_fd < 0) return;

    /* Non-blocking peek for first magic byte */
    int n = read(s_uart_fd, &byte, 1);
    if (n <= 0) return;
    if (byte != magic[0]) return;

    /* Read remaining magic with blocking reads (short data, should be immediate) */
    uint8_t rest[3];
    size_t got = 0;
    while (got < 3) {
        n = read(s_uart_fd, &rest[got], 3 - got);
        if (n <= 0) return;
        got += (size_t) n;
    }
    if (rest[0] != magic[1] || rest[1] != magic[2] || rest[2] != magic[3]) {
        return;
    }

    ESP_LOGI(TAG, "upload: magic received, expecting %u bytes...", (unsigned) fb->buffer_size);

    /* Switch to blocking mode for bulk data transfer */
    int flags = fcntl(s_uart_fd, F_GETFL);
    fcntl(s_uart_fd, F_SETFL, flags & ~O_NONBLOCK);

    /* Receive pixel data */
    size_t received = 0;
    while (received < fb->buffer_size) {
        n = read(s_uart_fd, &fb->buffer[received], fb->buffer_size - received);
        if (n <= 0) {
            ESP_LOGE(TAG, "upload: read error at byte %u/%u (n=%d)", (unsigned) received, (unsigned) fb->buffer_size,
                     n);
            byte = UPLOAD_NAK;
            write(s_uart_fd, &byte, 1);
            fcntl(s_uart_fd, F_SETFL, flags); /* restore non-blocking */
            return;
        }
        received += (size_t) n;
    }

    /* Restore non-blocking mode */
    fcntl(s_uart_fd, F_SETFL, flags);

    byte = UPLOAD_ACK;
    write(s_uart_fd, &byte, 1);
    ESP_LOGI(TAG, "upload: %u bytes received, framebuffer updated!", (unsigned) received);
}

/* ── O GORILA ────────────────────────────────────────────────────── */

static void draw_gorilla(crt_fb_surface_t *fb) {
    crt_fb_clear(fb, 0);

    int cx = fb->width / 2; /* 128 */
    int h = fb->height; /* ~243 */

    /* Body / torso */
    fb_ellipse(fb, cx, h * 65 / 100, 58, 62, 48);

    /* Arms */
    fb_ellipse(fb, cx - 68, h * 50 / 100, 22, 52, 48);
    fb_ellipse(fb, cx + 68, h * 50 / 100, 22, 52, 48);

    /* Hands */
    fb_ellipse(fb, cx - 72, h * 75 / 100, 16, 12, 58);
    fb_ellipse(fb, cx + 72, h * 75 / 100, 16, 12, 58);

    /* Chest patch (lighter fur) */
    fb_ellipse(fb, cx, h * 68 / 100, 32, 36, 75);

    /* Neck */
    fb_rect(fb, cx - 25, h * 35 / 100, 50, 20, 48);

    /* Head (dark fur) */
    fb_ellipse(fb, cx, h * 24 / 100, 48, 46, 48);

    /* Ears */
    fb_ellipse(fb, cx - 48, h * 22 / 100, 11, 13, 48);
    fb_ellipse(fb, cx + 48, h * 22 / 100, 11, 13, 48);
    fb_ellipse(fb, cx - 48, h * 22 / 100, 6, 8, 85);
    fb_ellipse(fb, cx + 48, h * 22 / 100, 6, 8, 85);

    /* Brow ridge */
    fb_ellipse(fb, cx, h * 17 / 100, 38, 10, 38);

    /* Face (lighter skin) */
    fb_ellipse(fb, cx, h * 27 / 100, 33, 30, 105);

    /* Eyes — white */
    fb_ellipse(fb, cx - 15, h * 21 / 100, 10, 6, 200);
    fb_ellipse(fb, cx + 15, h * 21 / 100, 10, 6, 200);

    /* Pupils */
    fb_ellipse(fb, cx - 13, h * 21 / 100, 5, 5, 18);
    fb_ellipse(fb, cx + 13, h * 21 / 100, 5, 5, 18);

    /* Eye highlights */
    fb_ellipse(fb, cx - 15, h * 20 / 100, 2, 2, 255);
    fb_ellipse(fb, cx + 15, h * 20 / 100, 2, 2, 255);

    /* Nose */
    fb_ellipse(fb, cx, h * 30 / 100, 16, 10, 80);
    fb_ellipse(fb, cx - 6, h * 31 / 100, 3, 3, 28);
    fb_ellipse(fb, cx + 6, h * 31 / 100, 3, 3, 28);

    /* Mouth */
    fb_ellipse(fb, cx, h * 36 / 100, 12, 4, 28);

    /* Teeth — grinning! */
    fb_rect(fb, cx - 8, h * 35 / 100, 16, 3, 220);

    /* Belly button lol */
    fb_ellipse(fb, cx, h * 72 / 100, 3, 3, 55);
}

static esp_err_t app_start_core(crt_video_standard_t video_standard)
{
    crt_core_config_t config = {
        .video_standard = video_standard,
        .enable_color = k_enable_color,
        .demo_pattern_mode = k_enable_color ? CRT_DEMO_PATTERN_COLOR_BARS_RAMP : CRT_DEMO_PATTERN_LUMA_BARS,
        .target_ready_depth = 8,
        .min_ready_depth = 0,
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

            /* Load image into framebuffer */
            memcpy(s_fb.buffer, godzilla_pixels, s_fb.buffer_size);

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

    /* Configure UART0 for non-blocking reads (upload protocol) */
    uart_driver_install(UPLOAD_UART_NUM, UPLOAD_RX_BUF, 0, 0, NULL, 0);
    esp_vfs_dev_uart_use_driver(UPLOAD_UART_NUM);
    s_uart_fd = open("/dev/uart/0", O_RDWR | O_NONBLOCK);
    if (s_uart_fd >= 0) {
        ESP_LOGI(TAG, "UART upload listener ready (fd=%d)", s_uart_fd);
    } else {
        /* Fallback: use stdin fd with fcntl non-block */
        s_uart_fd = fileno(stdin);
        fcntl(s_uart_fd, F_SETFL, fcntl(s_uart_fd, F_GETFL) | O_NONBLOCK);
        ESP_LOGI(TAG, "UART upload listener on stdin (fd=%d)", s_uart_fd);
    }

    /* Main loop: diagnostics + serial upload check */
    uint32_t diag_tick = 0;
    while (true) {
        /* Check for incoming image upload (non-blocking) */
        uart_upload_check(&s_fb);

        vTaskDelay(pdMS_TO_TICKS(50)); /* 20Hz poll */

        /* Diagnostic: log DMA underruns every 5s (100 ticks * 50ms) */
        if (++diag_tick >= 100) {
            diag_tick = 0;
            crt_diag_snapshot_t diag;
            crt_core_get_diag_snapshot(&diag);
            ESP_LOGI(TAG, "diag: underruns=%"PRIu32" queue_min=%"PRIu32" prep_max=%"PRIu32" cycles",
                     diag.dma_underrun_count, diag.ready_queue_min_depth, diag.prep_cycles_max);
        }
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

        /* Free framebuffer before deinit to avoid leak on re-init */
        crt_register_scanline_hook(NULL, NULL);
        crt_fb_surface_deinit(&s_fb);

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
