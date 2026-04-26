#include "crt_compose.h"
#include "crt_compose_layers.h"
#include "crt_core.h"
#include "crt_fb.h"
#include "crt_sprite.h"
#include "crt_stimulus.h"
#include "crt_tile.h"

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "godzilla_img.h"
#include "sdkconfig.h"
#include "tile_demo.h"

#if CONFIG_CRT_ENABLE_UART_UPLOAD
#include "driver/uart.h"
#include "driver/uart_vfs.h"

#include <fcntl.h>
#include <unistd.h>
#endif

static const char *TAG = "app_main";
static const bool k_enable_color = CONFIG_CRT_ENABLE_COLOR;
#if CONFIG_CRT_RENDER_MODE_RGB332_FB
static const bool k_use_rgb332_framebuffer = CONFIG_CRT_RENDER_MODE_RGB332_FB;
#else
static const bool k_use_rgb332_framebuffer = false;
#endif
#if CONFIG_CRT_RENDER_MODE_STIMULUS
static const bool k_use_stimulus = CONFIG_CRT_RENDER_MODE_STIMULUS;
#else
static const bool k_use_stimulus = false;
#endif
static crt_fb_surface_t s_fb;
static crt_compose_t s_compose;
static crt_stimulus_t s_stimulus;
static crt_tile_layer_t s_tile;

/* Demo scene:
 *   layer 0 fused: tile (horizontal scroll per frame)
 *   layer 1 keyed: crt_sprite_layer with 4 bouncing 16x16 sprites
 * Stays on the 1+1 fast path so the prep budget keeps the 0-underrun
 * invariant the hardware just re-validated. */
static crt_compose_viewport_layer_t s_viewport_god; /* reserved, disabled */
static crt_compose_checker_layer_t s_checker;       /* reserved, disabled */
static crt_compose_rect_layer_t s_hud_rect;         /* reserved, disabled */
static uint8_t s_checker_layer_idx = CRT_COMPOSE_LAYER_INVALID;

/* 4 sprites, 16x16 each, laid out side-by-side in a 64x16 atlas.
 * Filled with distinct grayscale levels so each sprite is visually
 * unmistakable against the tile pattern. DRAM_ATTR keeps the data
 * out of flash so per-line fetches do not stall on cache misses. */
DRAM_ATTR static uint8_t s_sprite_atlas_data[64 * 16];
static crt_sprite_atlas_t s_sprite_atlas;
static crt_sprite_layer_t s_sprite_layer;
enum {
    APP_DEMO_SPRITE_COUNT = 4,
};
static uint8_t s_sprite_ids[APP_DEMO_SPRITE_COUNT];

static void demo_sprite_atlas_fill(void)
{
    static const uint8_t k_colors[APP_DEMO_SPRITE_COUNT] = {64U, 128U, 192U, 240U};
    for (size_t s = 0; s < APP_DEMO_SPRITE_COUNT; ++s) {
        for (uint8_t y = 0; y < 16U; ++y) {
            uint8_t *row = &s_sprite_atlas_data[(size_t)y * 64U + (size_t)s * 16U];
            for (uint8_t x = 0; x < 16U; ++x) {
                /* 1px transparent border + filled interior: gives the
                 * sprite a visible silhouette when overlapping the BG. */
                row[x] = (x == 0 || x == 15 || y == 0 || y == 15) ? 0U : k_colors[s];
            }
        }
    }
}

/* Tile backend storage. Nametable is DRAM-resident (mutable at runtime);
 * pattern_table lives in rodata via tile_demo.h. 32x32 pitch enables the
 * AND-mask wraparound fast path; 32x30 visible matches NTSC/PAL active
 * lines exactly so the compose hot path lands on the 256->768 expansion. */
#define TILE_PITCH_W   32u
#define TILE_PITCH_H   32u
#define TILE_VISIBLE_W 32u
#define TILE_VISIBLE_H 30u
static uint8_t s_tile_nametable[TILE_PITCH_W * TILE_PITCH_H];

#define APP_FB_WIDTH    256
#define APP_FB_HEIGHT   240
#define APP_BLANK_LEVEL ((uint16_t)(23U << 8))
#define APP_WHITE_LEVEL ((uint16_t)(0x70U << 8)) /* ~44% DAC — tuned for C270 webcam capture */

/* Frame hook: drives runtime animation + mutation API exercises.
 * - Tile horizontal scroll wraps every visible_w*8 pixels.
 * - Each sprite bounces with its own per-frame delta via
 *   crt_sprite_move_by (sprite mutation API). */
IRAM_ATTR static void demo_frame_hook(uint32_t frame, void *user_data)
{
    (void)user_data;
    (void)frame;

    crt_tile_set_scroll(&s_tile, (int)(frame % (TILE_VISIBLE_W * 8U)), 0);

    /* Sprite world is logical 256x240 (before x_scale=3). Bounce inside
     * [0 .. 256-16] horizontally and [0 .. 240-16] vertically per sprite. */
    static int16_t s_dx[APP_DEMO_SPRITE_COUNT] = {1, -1, 2, -2};
    static int16_t s_dy[APP_DEMO_SPRITE_COUNT] = {1, 2, -1, -2};
    for (size_t i = 0; i < APP_DEMO_SPRITE_COUNT; ++i) {
        if (s_sprite_ids[i] == CRT_SPRITE_INVALID_ID) {
            continue;
        }
        crt_sprite_t spr;
        if (crt_sprite_get(&s_sprite_layer, s_sprite_ids[i], &spr) != ESP_OK) {
            continue;
        }
        int16_t nx = (int16_t)(spr.x + s_dx[i]);
        int16_t ny = (int16_t)(spr.y + s_dy[i]);
        if (nx <= 0 || nx >= (int16_t)(256 - 16)) {
            s_dx[i] = (int16_t)-s_dx[i];
        }
        if (ny <= 0 || ny >= (int16_t)(240 - 16)) {
            s_dy[i] = (int16_t)-s_dy[i];
        }
        crt_sprite_move_by(&s_sprite_layer, s_sprite_ids[i], s_dx[i], s_dy[i]);
    }
}

IRAM_ATTR static void stimulus_frame_hook(uint32_t frame, void *user_data)
{
    (void)user_data;
    crt_stimulus_set_frame(&s_stimulus, frame);
}

static void app_fill_rgb332_test_card(crt_fb_surface_t *fb)
{
    if (fb == NULL || fb->buffer == NULL || fb->width == 0 || fb->height == 0) {
        return;
    }

    for (uint16_t y = 0; y < fb->height; ++y) {
        uint8_t *row = crt_fb_row(fb, y);
        uint8_t red = (uint8_t)(((uint32_t)y * 8U / fb->height) << 5);
        for (uint16_t x = 0; x < fb->width; ++x) {
            row[x] = (uint8_t)(red | ((uint32_t)x * 32U / fb->width));
        }
    }
}

/* ── Optional serial upload protocol ─────────────────────────────── */
#if CONFIG_CRT_ENABLE_UART_UPLOAD
#define UPLOAD_MAGIC    {0xFB, 0xDA, 0x00, 0x01}
#define UPLOAD_ACK      0x06
#define UPLOAD_NAK      0x15
#define UPLOAD_UART_NUM UART_NUM_0
#define UPLOAD_RX_BUF   8192

static int s_uart_fd = -1;

static esp_err_t uart_upload_init(void)
{
    if (s_uart_fd >= 0) {
        close(s_uart_fd);
        s_uart_fd = -1;
    }

    uart_vfs_dev_use_driver(UPLOAD_UART_NUM);
    s_uart_fd = open("/dev/uart/0", O_RDWR | O_NONBLOCK);
    if (s_uart_fd < 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "uart upload enabled on /dev/uart/0");
    return ESP_OK;
}

static void uart_upload_check(crt_fb_surface_t *fb)
{
    uint8_t byte;
    static const uint8_t magic[] = UPLOAD_MAGIC;

    if (s_uart_fd < 0)
        return;

    /* Non-blocking peek for first magic byte */
    int n = read(s_uart_fd, &byte, 1);
    if (n <= 0)
        return;
    if (byte != magic[0])
        return;

    /* Read remaining magic with blocking reads (short data, should be immediate) */
    uint8_t rest[3];
    size_t got = 0;
    while (got < 3) {
        n = read(s_uart_fd, &rest[got], 3 - got);
        if (n <= 0)
            return;
        got += (size_t)n;
    }
    if (rest[0] != magic[1] || rest[1] != magic[2] || rest[2] != magic[3]) {
        return;
    }

    ESP_LOGI(TAG, "upload: magic received, expecting %u bytes...", (unsigned)fb->buffer_size);

    /* Switch to blocking mode for bulk data transfer */
    int flags = fcntl(s_uart_fd, F_GETFL);
    fcntl(s_uart_fd, F_SETFL, flags & ~O_NONBLOCK);

    /* Receive pixel data */
    size_t received = 0;
    while (received < fb->buffer_size) {
        n = read(s_uart_fd, &fb->buffer[received], fb->buffer_size - received);
        if (n <= 0) {
            ESP_LOGE(TAG, "upload: read error at byte %u/%u (n=%d)", (unsigned)received,
                     (unsigned)fb->buffer_size, n);
            byte = UPLOAD_NAK;
            write(s_uart_fd, &byte, 1);
            fcntl(s_uart_fd, F_SETFL, flags); /* restore non-blocking */
            return;
        }
        received += (size_t)n;
    }

    /* Restore non-blocking mode */
    fcntl(s_uart_fd, F_SETFL, flags);

    byte = UPLOAD_ACK;
    write(s_uart_fd, &byte, 1);
    ESP_LOGI(TAG, "upload: %u bytes received, framebuffer updated!", (unsigned)received);
}

#if CONFIG_CRT_TEST_STANDARD_TOGGLE
static void uart_upload_shutdown(void)
{
    if (s_uart_fd >= 0) {
        close(s_uart_fd);
        s_uart_fd = -1;
    }
}
#endif
#endif

static esp_err_t app_start_core(crt_video_standard_t video_standard)
{
    crt_core_config_t config = {
        .video_standard = video_standard,
        .enable_color = k_enable_color || k_use_rgb332_framebuffer,
        .demo_pattern_mode = (k_enable_color || k_use_rgb332_framebuffer)
                                 ? CRT_DEMO_PATTERN_COLOR_BARS_RAMP
                                 : CRT_DEMO_PATTERN_LUMA_BARS,
        .target_ready_depth = 48,
        .min_ready_depth = 0,
        .prep_task_core = 1,
    };

    esp_err_t err = crt_core_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "crt_core_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = crt_fb_surface_init(&s_fb, APP_FB_WIDTH, APP_FB_HEIGHT, CRT_FB_FORMAT_INDEXED8);
    if (err == ESP_OK) {
        err = crt_fb_surface_alloc(&s_fb);
    }
    if (err == ESP_OK) {
        if (k_use_rgb332_framebuffer) {
            app_fill_rgb332_test_card(&s_fb);
        } else if (k_use_stimulus) {
            crt_fb_palette_init_grayscale(&s_fb, APP_BLANK_LEVEL, APP_WHITE_LEVEL);
        } else {
            crt_fb_palette_init_grayscale(&s_fb, APP_BLANK_LEVEL, APP_WHITE_LEVEL);
            if (s_fb.buffer_size <= sizeof(godzilla_pixels)) {
                memcpy(s_fb.buffer, godzilla_pixels, s_fb.buffer_size);
            }
        }
#if CONFIG_CRT_ENABLE_UART_UPLOAD
        esp_err_t upload_err = uart_upload_init();
        if (upload_err != ESP_OK) {
            ESP_LOGW(TAG, "uart upload disabled: %s", esp_err_to_name(upload_err));
        }
#endif
    } else {
        ESP_LOGE(TAG, "fb alloc failed: %s", esp_err_to_name(err));
        return err;
    }

    if (k_use_rgb332_framebuffer) {
        crt_register_scanline_hook(crt_fb_rgb332_scanline_hook, &s_fb);
        ESP_LOGI(TAG, "render: direct RGB332 framebuffer %ux%u", s_fb.width, s_fb.height);
    } else if (k_use_stimulus) {
        crt_stimulus_config_t stimulus_config;
        crt_stimulus_default_config(&stimulus_config);
        stimulus_config.height = APP_FB_HEIGHT;
        stimulus_config.pattern = CRT_STIMULUS_PATTERN_FRAME_MARKERS;
        stimulus_config.cell_w = 16;
        stimulus_config.cell_h = 8;

        esp_err_t stimulus_err = crt_stimulus_init(&s_stimulus, &stimulus_config);
        if (stimulus_err != ESP_OK) {
            ESP_LOGE(TAG, "crt_stimulus_init failed: %s", esp_err_to_name(stimulus_err));
            return stimulus_err;
        }

        crt_compose_init(&s_compose);
        crt_compose_set_palette(&s_compose, s_fb.palette, s_fb.palette_size);
        crt_compose_add_layer(&s_compose, crt_stimulus_layer_fetch, &s_stimulus,
                              CRT_COMPOSE_NO_TRANSPARENCY);
        crt_register_scanline_hook(crt_compose_scanline_hook, &s_compose);
        crt_register_frame_hook(stimulus_frame_hook, NULL);
        ESP_LOGI(TAG, "render: measurement stimulus compositor %ux%u", APP_FB_WIDTH, APP_FB_HEIGHT);
    } else {
        /* ── Tile layer as fused base + keyed overlay on top ──────────── */

        /* Tile layer: 32x32 pitch (PoT), 32x30 visible. Pattern from
         * rodata (tile_demo.h); nametable filled in DRAM. */
        tile_demo_fill_nametable(s_tile_nametable, TILE_PITCH_W);
        esp_err_t tile_err =
            crt_tile_init(&s_tile, TILE_VISIBLE_W, TILE_VISIBLE_H, TILE_PITCH_W, TILE_PITCH_H,
                          tile_demo_patterns, TILE_DEMO_COUNT, s_tile_nametable);
        if (tile_err != ESP_OK) {
            ESP_LOGE(TAG, "crt_tile_init failed: %s", esp_err_to_name(tile_err));
            return tile_err;
        }
        crt_tile_set_palette(&s_tile, s_fb.palette);

        /* Compositor scene: 4 layers exercising every primitive at once. */
        crt_compose_init(&s_compose);
        crt_compose_set_palette(&s_compose, s_fb.palette, s_fb.palette_size);

        /* Layer 0: tile (fused base, animated horizontal scroll). */
        crt_compose_add_layer_fused(&s_compose, crt_tile_layer_fetch, crt_tile_scanline_hook,
                                    &s_tile);

        /* Layer 1: sprite layer (single keyed layer holding the OAM).
         * x_scale=3 maps the 256-px logical sprite world into the 768
         * DAC samples per active line without instantiating extra layers. */
        demo_sprite_atlas_fill();
        crt_sprite_atlas_init(&s_sprite_atlas, s_sprite_atlas_data, 64U, 16U, 64U);
        crt_sprite_layer_init(&s_sprite_layer, &s_sprite_atlas, /* key */ 0);
        crt_sprite_layer_set_max_sprites_per_line(&s_sprite_layer, CRT_SPRITE_DEFAULT_PERLINE);
        crt_sprite_layer_set_x_scale(&s_sprite_layer, 3U);

        for (size_t i = 0; i < APP_DEMO_SPRITE_COUNT; ++i) {
            const int16_t spawn_x = (int16_t)(40 + i * 56);
            const int16_t spawn_y = (int16_t)(48 + i * 36); /* 48,84,120,156 — non-overlapping */
            esp_err_t spr_err = crt_sprite_add(&s_sprite_layer,
                                               /* cell_x */ (uint16_t)(i * 2U),
                                               /* cell_y */ 0U, CRT_SPRITE_SIZE_16X16, spawn_x,
                                               spawn_y, &s_sprite_ids[i]);
            if (spr_err != ESP_OK) {
                ESP_LOGW(TAG, "sprite_add %u failed: %s", i, esp_err_to_name(spr_err));
                s_sprite_ids[i] = CRT_SPRITE_INVALID_ID;
            }
        }

        crt_compose_add_layer(&s_compose, crt_sprite_layer_fetch, &s_sprite_layer, /* key */ 0);

        /* Reserved primitives (kept declared for the next iteration). */
        (void)s_viewport_god;
        (void)s_checker;
        (void)s_hud_rect;
        s_checker_layer_idx = CRT_COMPOSE_LAYER_INVALID;

        crt_register_scanline_hook(crt_compose_scanline_hook, &s_compose);
        crt_register_frame_hook(demo_frame_hook, NULL);
        ESP_LOGI(TAG, "compose: tile %ux%u (h-scroll) + sprite layer (%u sprites, max/line=%u)",
                 TILE_VISIBLE_W, TILE_VISIBLE_H, (unsigned)APP_DEMO_SPRITE_COUNT,
                 (unsigned)CRT_SPRITE_DEFAULT_PERLINE);
    }

    err = crt_core_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "crt_core_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "ESP32 CRT signal core started: standard=%s color=%s pattern=%s",
             (video_standard == CRT_VIDEO_STANDARD_PAL) ? "PAL" : "NTSC",
             config.enable_color ? "on" : "off",
             k_use_rgb332_framebuffer                                         ? "rgb332_fb"
             : k_use_stimulus                                                 ? "stimulus"
             : (config.demo_pattern_mode == CRT_DEMO_PATTERN_COLOR_BARS_RAMP) ? "color_bars_ramp"
                                                                              : "luma_bars");

    return ESP_OK;
}

static void app_log_diag_snapshot(void)
{
    crt_diag_snapshot_t diag;
    crt_core_get_diag_snapshot(&diag);
    ESP_LOGI(TAG, "diag: underruns=%" PRIu32 " queue_min=%" PRIu32 " prep_max=%" PRIu32 " cycles",
             diag.dma_underrun_count, diag.ready_queue_min_depth, diag.prep_cycles_max);
}

#if !CONFIG_CRT_TEST_STANDARD_TOGGLE
static void app_run_diag_loop(void)
{
    while (true) {
#if CONFIG_CRT_ENABLE_UART_UPLOAD
        uart_upload_check(&s_fb);
        vTaskDelay(pdMS_TO_TICKS(10));
#else
        vTaskDelay(pdMS_TO_TICKS(5000));
#endif

        static uint32_t ticks_since_diag;
        ticks_since_diag +=
#if CONFIG_CRT_ENABLE_UART_UPLOAD
            10U;
#else
            5000U;
#endif
        if (ticks_since_diag >= 5000U) {
            ticks_since_diag = 0;
            app_log_diag_snapshot();
        }
    }
}
#endif

#if CONFIG_CRT_TEST_STANDARD_TOGGLE
static void app_cleanup_core(void)
{

#if CONFIG_CRT_ENABLE_UART_UPLOAD
    uart_upload_shutdown();
#endif
    crt_register_scanline_hook(NULL, NULL);
    crt_register_frame_hook(NULL, NULL);
    crt_compose_clear_layers(&s_compose);
    crt_fb_surface_deinit(&s_fb);
}

static void app_run_standard_toggle_loop(crt_video_standard_t video_standard)
{
    while (true) {
        esp_err_t err;

#if CONFIG_CRT_ENABLE_UART_UPLOAD
        uart_upload_check(&s_fb);
#endif
        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_CRT_TEST_STANDARD_TOGGLE_INTERVAL_S * 1000U));
        video_standard = (video_standard == CRT_VIDEO_STANDARD_PAL) ? CRT_VIDEO_STANDARD_NTSC
                                                                    : CRT_VIDEO_STANDARD_PAL;

        ESP_LOGI(TAG, "test toggle: switching standard to %s (interval=%" PRIu32 "s)",
                 (video_standard == CRT_VIDEO_STANDARD_PAL) ? "PAL" : "NTSC",
                 (uint32_t)CONFIG_CRT_TEST_STANDARD_TOGGLE_INTERVAL_S);

        err = crt_core_stop();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "crt_core_stop failed during toggle: %s", esp_err_to_name(err));
            break;
        }

        app_cleanup_core();

        err = crt_core_deinit();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "crt_core_deinit failed during toggle: %s", esp_err_to_name(err));
            break;
        }
        if (app_start_core(video_standard) != ESP_OK) {
            break;
        }
    }
}
#endif

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
    app_run_standard_toggle_loop(video_standard);
#else
    app_run_diag_loop();
#endif
}
