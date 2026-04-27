#include "crt_compose.h"
#include "crt_compose_layers.h"
#include "crt_core.h"
#include "crt_fb.h"
#include "crt_sprite.h"
#include "crt_stimulus.h"
#include "crt_tile.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <math.h>
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
    APP_DEMO_SPRITE_COUNT = 3,
};
static uint8_t s_sprite_ids[APP_DEMO_SPRITE_COUNT];

static void demo_sprite_atlas_fill(void)
{
    static const uint8_t k_colors[APP_DEMO_SPRITE_COUNT] = {64U, 160U, 240U};
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
    static int16_t s_dx[APP_DEMO_SPRITE_COUNT] = {1, -1, 2};
    static int16_t s_dy[APP_DEMO_SPRITE_COUNT] = {1, 2, -1};
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

/* PRC calibration mode pre-rasterizes the static target into the indexed-8
 * surface and reuses crt_fb_scanline_hook (validated 0-underrun hot path).
 * Dynamic stimulus cycler is intentionally disabled here while we get the
 * geometric alignment dialed in; a separate code path will re-enable it once
 * the prep-task budget is profiled with stimulus loaded. */

IRAM_ATTR __attribute__((unused)) static void stimulus_frame_hook(uint32_t frame, void *user_data)
{
    (void)user_data;
    crt_stimulus_set_frame(&s_stimulus, frame);

    /* PRC calibration cycler: rotate 5 patterns, 8 s each (~480 NTSC frames),
     * giving full system-identification sweep in 40 s.
     *  CHECKER       - geometric calibration (align IR ring to raster center)
     *  PRBS          - pseudo-random binary excitation for impulse response
     *  IMPULSE       - point-source transient response
     *  CHIRP         - frequency-domain sweep
     *  FRAME_MARKERS - temporal sync fiducials for capture alignment
     * Counter-based stage advance avoids a runtime divide every frame
     * inside this IRAM hook. */
    static const crt_stimulus_pattern_t kCycle[] = {
        CRT_STIMULUS_PATTERN_CHECKER, CRT_STIMULUS_PATTERN_PRBS, CRT_STIMULUS_PATTERN_IMPULSE,
        CRT_STIMULUS_PATTERN_CHIRP,   CRT_STIMULUS_PATTERN_FRAME_MARKERS,
    };
    static uint32_t s_frame_in_stage = 0;
    static uint8_t s_stage = 0;
    if (++s_frame_in_stage >= 480U) {
        s_frame_in_stage = 0;
        s_stage = (uint8_t)((s_stage + 1U) % (sizeof(kCycle) / sizeof(kCycle[0])));
        s_stimulus.config.pattern = kCycle[s_stage];
    }
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
static const uint8_t k_upload_magic[4] = {0xFB, 0xDA, 0x00, 0x01};
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
    const uint8_t *magic = k_upload_magic;

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

/* Physical scale of the IR ring + assumed visible CRT face.
 * Tweak APP_CRT_VISIBLE_W_MM / APP_CRT_VISIBLE_H_MM for your tube; 5"
 * defaults to ~100 x 75 mm. The overlay renders the ring at true 40 mm
 * scale so the LEDs land exactly on the markers when you press the ring
 * onto the glass. */
#define APP_RING_OD_MM       40.0f
#define APP_RING_LED_COUNT   11
#define APP_CRT_VISIBLE_W_MM 275.0f
#define APP_CRT_VISIBLE_H_MM 206.0f
/* Optical centroid (in FB pixels) measured from the XY map gaussian fit.
 * Override here when the ring is repositioned. The indicator + decay + PRBS
 * disks all use this center so the IR LEDs land on top of the markers. */
#define APP_RING_CENTER_X    135
#define APP_RING_CENTER_Y    125

static inline void app_overlay_put(crt_fb_surface_t *fb, int x, int y, uint8_t idx)
{
    if (x < 0 || y < 0 || (uint16_t)x >= fb->width || (uint16_t)y >= fb->height) {
        return;
    }
    crt_fb_put(fb, (uint16_t)x, (uint16_t)y, idx);
}

static void app_overlay_fill_rect(crt_fb_surface_t *fb, int x0, int y0, int w, int h, uint8_t idx)
{
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            app_overlay_put(fb, x0 + dx, y0 + dy, idx);
        }
    }
}

/* Static guide telling the user exactly where to glue the IR ring on the
 * CRT face. The 256x240 framebuffer is mapped onto a 4:3 raster, so the
 * 40 mm ring becomes an ellipse in FB coordinates (pixels are wider than
 * they are tall). The overlay is drawn on a clean black background. */
static void app_draw_ring_indicator(crt_fb_surface_t *fb)
{
    if (fb == NULL || fb->buffer == NULL || fb->width == 0 || fb->height == 0) {
        return;
    }
    crt_fb_clear(fb, 0);

    const int cx = APP_RING_CENTER_X;
    const int cy = APP_RING_CENTER_Y;
    const float radius_mm = APP_RING_OD_MM * 0.5f;
    const int rx = (int)(radius_mm * (float)fb->width / APP_CRT_VISIBLE_W_MM + 0.5f);
    const int ry = (int)(radius_mm * (float)fb->height / APP_CRT_VISIBLE_H_MM + 0.5f);

    const uint8_t IDX_WHT = 255;

    /* Crosshair: 33 px x 1 px white core (pure white on black) */
    app_overlay_fill_rect(fb, cx - 14, cy, 29, 1, IDX_WHT);
    app_overlay_fill_rect(fb, cx, cy - 14, 1, 29, IDX_WHT);

    /* Elliptical outline of the ring (72 angular samples, 1-px white) */
    for (int t = 0; t < 72; ++t) {
        const float a = (float)t * (2.0f * (float)M_PI / 72.0f);
        const int x = cx + (int)(rx * cosf(a) + 0.5f);
        const int y = cy + (int)(ry * sinf(a) + 0.5f);
        app_overlay_put(fb, x, y, IDX_WHT);
    }

    /* 11 LED markers on the outline — 5x5 white squares for high contrast. */
    for (int k = 0; k < APP_RING_LED_COUNT; ++k) {
        const float a = (float)k * (2.0f * (float)M_PI / (float)APP_RING_LED_COUNT);
        const int mx = cx + (int)(rx * cosf(a) + 0.5f);
        const int my = cy + (int)(ry * sinf(a) + 0.5f);
        app_overlay_fill_rect(fb, mx - 2, my - 2, 5, 5, IDX_WHT);
    }
}

/* ── Single-pin IR ring driver + ambient sensor (port from devkit-v1) ──
 * GPIO32 dual-mode: OUTPUT drives the XZ511c-06 ring HIGH/LOW; INPUT +
 * ADC1_CH4 reads ambient luminance through the same pad. GPIO32 is in a
 * different domain than GPIO25/I2S0/DAC, so it runs free of the composite
 * pipeline. The task is pinned to Core 0 so it never steals cycles from
 * the prep_task on Core 1. */
#define APP_IR_PIN          GPIO_NUM_32
#define APP_IR_ADC_UNIT     ADC_UNIT_1
#define APP_IR_ADC_CHANNEL  ADC_CHANNEL_4
#define APP_IR_SAMPLE_MS    100U
#define APP_IR_BURST_N      16
#define APP_IR_DRAIN_MS     20U

typedef enum {
    APP_IR_MODE_AUTO = 0,
    APP_IR_MODE_FORCE_ON,
    APP_IR_MODE_FORCE_OFF,
    APP_IR_MODE_MONITOR,
} app_ir_mode_t;

static adc_oneshot_unit_handle_t s_ir_adc = NULL;
static bool s_ir_ring_on = false;
static int s_ir_baseline = 0;
static int s_ir_threshold = 200;
static int s_ir_hysteresis = 80;
static app_ir_mode_t s_ir_mode = APP_IR_MODE_MONITOR; /* manual toggle below */

static void app_ir_ring_set(bool on)
{
    gpio_set_direction(APP_IR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(APP_IR_PIN, on ? 1 : 0);
    s_ir_ring_on = on;
}

static int app_ir_ring_measure(void)
{
    const bool was_on = s_ir_ring_on;
    /* Drain phase: force pad LOW to discharge the parasitic capacitance
     * left by the previous OUTPUT state. */
    gpio_set_direction(APP_IR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(APP_IR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(APP_IR_DRAIN_MS));

    /* Sense phase: pad as input with internal pull-down so dark = ~0. */
    gpio_set_direction(APP_IR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(APP_IR_PIN, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(500);

    long sum = 0;
    for (int i = 0; i < APP_IR_BURST_N; ++i) {
        int raw = 0;
        adc_oneshot_read(s_ir_adc, APP_IR_ADC_CHANNEL, &raw);
        sum += raw;
        esp_rom_delay_us(200);
    }
    const int v = (int)(sum / APP_IR_BURST_N);

    app_ir_ring_set(was_on);
    return v;
}

static int app_ir_capture_window(uint32_t window_ms, int *out_min, int *out_max)
{
    long sum = 0;
    int n = 0;
    int vmin = 4095;
    int vmax = 0;
    const int64_t end_ms = (esp_timer_get_time() / 1000) + window_ms;
    while ((esp_timer_get_time() / 1000) < end_ms) {
        const int v = app_ir_ring_measure();
        sum += v;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        n++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (out_min) *out_min = vmin;
    if (out_max) *out_max = vmax;
    return (n > 0) ? (int)(sum / n) : 0;
}

/* Fills the entire indexed-8 framebuffer with a single palette index.
 * memset is fine because the surface is contiguous bytes at width pitch. */
static void app_fb_fill(crt_fb_surface_t *fb, uint8_t idx)
{
    if (fb == NULL || fb->buffer == NULL) {
        return;
    }
    memset(fb->buffer, idx, fb->buffer_size);
}

/* Fills an axis-aligned ellipse at (cx, cy) with semi-axes (rx, ry) using
 * palette index `idx`. Used to light a disk that maps the IR ring footprint
 * onto the phosphor for impulse-response measurement. */
static void app_fb_fill_ellipse(crt_fb_surface_t *fb, int cx, int cy, int rx, int ry, uint8_t idx)
{
    if (fb == NULL || fb->buffer == NULL || rx <= 0 || ry <= 0) {
        return;
    }
    const long rx2 = (long)rx * rx;
    const long ry2 = (long)ry * ry;
    for (int dy = -ry; dy <= ry; ++dy) {
        const long term_y = (long)dy * dy * rx2;
        for (int dx = -rx; dx <= rx; ++dx) {
            if ((long)dx * dx * ry2 + term_y <= rx2 * ry2) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x >= 0 && y >= 0 && (uint16_t)x < fb->width && (uint16_t)y < fb->height) {
                    crt_fb_put(fb, (uint16_t)x, (uint16_t)y, idx);
                }
            }
        }
    }
}

/* Burst-sample ADC1_CH4 directly (no drain, pad already in INPUT mode).
 * Returns by writing into out_buf. Period is best-effort; ADC oneshot
 * read takes ~30-50 us so the floor is around 20 kS/s. */
static void app_ir_burst_sample(int *out_buf, int n, uint32_t period_us)
{
    for (int i = 0; i < n; ++i) {
        adc_oneshot_read(s_ir_adc, APP_IR_ADC_CHANNEL, &out_buf[i]);
        if (period_us > 0) {
            esp_rom_delay_us(period_us);
        }
    }
}

static void app_ir_ring_calibrate(void)
{
    /* CRT-coupled calibration: the IR ring is glued to the CRT face, so
     * the LEDs in the ring sit micrometers from the phosphor and act as
     * photodiodes when GPIO32 floats. We drive the whole framebuffer to
     * pure white for 2 s (CLARO) then pure black for 2 s (ESCURO) and
     * record the ADC swing. */
    app_ir_ring_set(false);

    ESP_LOGI(TAG, "IR cal: CRT FULL WHITE, ring on glass — measuring 2 s...");
    app_fb_fill(&s_fb, 255);
    vTaskDelay(pdMS_TO_TICKS(500)); /* phosphor + ADC settle */
    int claro_min, claro_max;
    const int claro_mean = app_ir_capture_window(2000, &claro_min, &claro_max);
    ESP_LOGI(TAG, "IR cal: CLARO  min=%4d max=%4d mean=%4d", claro_min, claro_max, claro_mean);

    ESP_LOGI(TAG, "IR cal: CRT FULL BLACK — measuring 2 s...");
    app_fb_fill(&s_fb, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    int escuro_min, escuro_max;
    const int escuro_mean = app_ir_capture_window(2000, &escuro_min, &escuro_max);
    ESP_LOGI(TAG, "IR cal: ESCURO min=%4d max=%4d mean=%4d", escuro_min, escuro_max, escuro_mean);

    const int swing = claro_mean - escuro_mean;
    const int abs_swing = (swing < 0) ? -swing : swing;
    s_ir_baseline = (claro_mean + escuro_mean) / 2;
    s_ir_threshold = (abs_swing > 0) ? (abs_swing / 4) : 200;
    s_ir_hysteresis = (abs_swing > 0) ? (abs_swing / 8) : 80;
    ESP_LOGI(TAG, "IR cal: swing=%+d baseline=%d threshold=%d hyst=%d", swing, s_ir_baseline,
             s_ir_threshold, s_ir_hysteresis);
    if (abs_swing < 30) {
        ESP_LOGW(TAG, "IR cal: |swing| < 30 LSB — ring may not be optically coupled to CRT");
    }

    /* Restore the ring placement indicator after calibration. */
    app_draw_ring_indicator(&s_fb);
}

/* 2D scan: lights a small disk at every (gx, gy) position of an NxN grid,
 * measures the ADC at each, then prints the matrix. The peak cell is the
 * true centroid of the IR ring's optical footprint on the CRT face. */
static void app_ir_xy_map(void)
{
    const int kGrid = 16;   /* 16x16 = 256 measurements */
    const int kProbeR = 6;  /* small disc, smaller than ring radius */
    const int kSettleMs = 80;
    const int kSampleN = 16;

    gpio_set_direction(APP_IR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(APP_IR_PIN, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(500);

    static int matrix[16][16] = {{0}};
    int peak_v = 0, peak_i = 0, peak_j = 0;
    long sum_w = 0;
    long sum_wx = 0;
    long sum_wy = 0;

    ESP_LOGI("xy_map", "starting %dx%d sweep, probe r=%d", kGrid, kGrid, kProbeR);
    for (int j = 0; j < kGrid; ++j) {
        const int cy = (int)(((long)j * (s_fb.height - 2 * kProbeR - 1)) / (kGrid - 1)) + kProbeR;
        for (int i = 0; i < kGrid; ++i) {
            const int cx = (int)(((long)i * (s_fb.width - 2 * kProbeR - 1)) / (kGrid - 1))
                           + kProbeR;
            app_fb_fill(&s_fb, 0);
            app_fb_fill_ellipse(&s_fb, cx, cy, kProbeR, kProbeR, 255);
            vTaskDelay(pdMS_TO_TICKS(kSettleMs));
            long sum = 0;
            for (int s = 0; s < kSampleN; ++s) {
                int v;
                adc_oneshot_read(s_ir_adc, APP_IR_ADC_CHANNEL, &v);
                sum += v;
                esp_rom_delay_us(200);
            }
            const int adc = (int)(sum / kSampleN);
            matrix[j][i] = adc;
            if (adc > peak_v) {
                peak_v = adc;
                peak_i = i;
                peak_j = j;
            }
            sum_w += adc;
            sum_wx += (long)adc * cx;
            sum_wy += (long)adc * cy;
        }
    }

    /* Print as one row per ESP_LOGI line for readability. */
    char line[160];
    for (int j = 0; j < kGrid; ++j) {
        int n = snprintf(line, sizeof(line), "row%2d:", j);
        for (int i = 0; i < kGrid; ++i) {
            n += snprintf(line + n, sizeof(line) - n, " %4d", matrix[j][i]);
        }
        ESP_LOGI("xy_csv", "%s", line);
    }
    const int peak_px =
        (int)(((long)peak_i * (s_fb.width - 2 * kProbeR - 1)) / (kGrid - 1)) + kProbeR;
    const int peak_py =
        (int)(((long)peak_j * (s_fb.height - 2 * kProbeR - 1)) / (kGrid - 1)) + kProbeR;
    ESP_LOGI("xy_map", "PEAK cell=(%d,%d) px=(%d,%d) ADC=%d", peak_i, peak_j, peak_px, peak_py,
             peak_v);
    if (sum_w > 0) {
        const long centroid_x = sum_wx / sum_w;
        const long centroid_y = sum_wy / sum_w;
        ESP_LOGI("xy_map", "WEIGHTED centroid px=(%ld,%ld)  fb=(%d,%d)", centroid_x, centroid_y,
                 s_fb.width, s_fb.height);
    }
    app_ir_ring_set(false);
}

/* Area sweep: light a thin vertical bar at column X (full height), measure
 * mean ADC; repeat for 16 X positions, then for 16 Y positions with a
 * horizontal bar. Output reveals the optical footprint of the ring on the
 * CRT face: peak = center of ring sensitivity, FWHM = effective radius. */
static void app_ir_area_sweep(void)
{
    const int kSteps = 16;
    const int kBarThickness = 8;
    const int kSettleMs = 60;
    const int kSampleN = 8;

    /* Pad in INPUT + pulldown for ADC reads */
    gpio_set_direction(APP_IR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(APP_IR_PIN, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(500);

    ESP_LOGI("area_x", "vertical-bar sweep across X (thickness=%d, %d positions)", kBarThickness,
             kSteps);
    int x_curve[16] = {0};
    for (int i = 0; i < kSteps; ++i) {
        const int x = (int)(((long)i * (s_fb.width - kBarThickness)) / (kSteps - 1));
        app_fb_fill(&s_fb, 0);
        for (int by = 0; by < (int)s_fb.height; ++by) {
            for (int bx = 0; bx < kBarThickness; ++bx) {
                crt_fb_put(&s_fb, (uint16_t)(x + bx), (uint16_t)by, 255);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kSettleMs));
        long sum = 0;
        for (int s = 0; s < kSampleN; ++s) {
            int v;
            adc_oneshot_read(s_ir_adc, APP_IR_ADC_CHANNEL, &v);
            sum += v;
            esp_rom_delay_us(200);
        }
        x_curve[i] = (int)(sum / kSampleN);
    }
    char line[160];
    int n = snprintf(line, sizeof(line), "ADC@x:");
    for (int i = 0; i < kSteps; ++i) {
        n += snprintf(line + n, sizeof(line) - n, " %4d", x_curve[i]);
    }
    ESP_LOGI("area_x", "%s", line);

    ESP_LOGI("area_y", "horizontal-bar sweep across Y (thickness=%d, %d positions)", kBarThickness,
             kSteps);
    int y_curve[16] = {0};
    for (int i = 0; i < kSteps; ++i) {
        const int y = (int)(((long)i * (s_fb.height - kBarThickness)) / (kSteps - 1));
        app_fb_fill(&s_fb, 0);
        for (int by = 0; by < kBarThickness; ++by) {
            for (int bx = 0; bx < (int)s_fb.width; ++bx) {
                crt_fb_put(&s_fb, (uint16_t)bx, (uint16_t)(y + by), 255);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kSettleMs));
        long sum = 0;
        for (int s = 0; s < kSampleN; ++s) {
            int v;
            adc_oneshot_read(s_ir_adc, APP_IR_ADC_CHANNEL, &v);
            sum += v;
            esp_rom_delay_us(200);
        }
        y_curve[i] = (int)(sum / kSampleN);
    }
    n = snprintf(line, sizeof(line), "ADC@y:");
    for (int i = 0; i < kSteps; ++i) {
        n += snprintf(line + n, sizeof(line) - n, " %4d", y_curve[i]);
    }
    ESP_LOGI("area_y", "%s", line);

    /* Pick peaks for quick interpretation */
    int x_peak_idx = 0, y_peak_idx = 0;
    for (int i = 1; i < kSteps; ++i) {
        if (x_curve[i] > x_curve[x_peak_idx]) x_peak_idx = i;
        if (y_curve[i] > y_curve[y_peak_idx]) y_peak_idx = i;
    }
    const int x_peak_px = (int)(((long)x_peak_idx * (s_fb.width - kBarThickness)) / (kSteps - 1));
    const int y_peak_px = (int)(((long)y_peak_idx * (s_fb.height - kBarThickness)) / (kSteps - 1));
    ESP_LOGI("area", "peak X=%d (px≈%d / %d) | Y=%d (px≈%d / %d)", x_peak_idx, x_peak_px,
             s_fb.width, y_peak_idx, y_peak_px, s_fb.height);

    app_ir_ring_set(false);
}

/* PRBS-driven CRT excitation with synchronized ADC sampling.
 * Each "bit" lights the central disk for 80 ms (white = 1) or holds it
 * black (0). After settling, mean ADC is captured. The result is a 64-bit
 * sequence and its measured response — used offline to compute the
 * impulse response of the CRT phosphor → IR ring system via cross-corr. */
static void app_ir_prbs_capture(void)
{
    const int kBits = 128;
    const int kSettleMs = 50;
    const int kSampleN = 8;
    /* 31-bit Galois LFSR seed; period 2^31-1 so 128 bits are pseudo-random. */
    uint32_t lfsr = 0xACE1u;

    const float radius_mm = APP_RING_OD_MM * 0.5f;
    const int cx = APP_RING_CENTER_X;
    const int cy = APP_RING_CENTER_Y;
    const int rx = (int)(radius_mm * (float)s_fb.width / APP_CRT_VISIBLE_W_MM + 0.5f);
    const int ry = (int)(radius_mm * (float)s_fb.height / APP_CRT_VISIBLE_H_MM + 0.5f);

    gpio_set_direction(APP_IR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(APP_IR_PIN, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(500);

    uint64_t bits_lo = 0; /* bits 0..63 */
    uint64_t bits_hi = 0; /* bits 64..127 */
    int adc_seq[128] = {0};
    for (int i = 0; i < kBits; ++i) {
        const uint32_t out = lfsr & 1U;
        lfsr = (lfsr >> 1) ^ (uint32_t)((-(int32_t)(out)) & 0xA3000000u);
        if (i < 64) {
            bits_lo |= ((uint64_t)out) << i;
        } else {
            bits_hi |= ((uint64_t)out) << (i - 64);
        }

        app_fb_fill(&s_fb, 0);
        if (out) {
            app_fb_fill_ellipse(&s_fb, cx, cy, rx, ry, 255);
        }
        vTaskDelay(pdMS_TO_TICKS(kSettleMs));
        long sum = 0;
        for (int s = 0; s < kSampleN; ++s) {
            int v;
            adc_oneshot_read(s_ir_adc, APP_IR_ADC_CHANNEL, &v);
            sum += v;
            esp_rom_delay_us(200);
        }
        adc_seq[i] = (int)(sum / kSampleN);
    }

    ESP_LOGI("prbs", "bits_lo=0x%016llx bits_hi=0x%016llx (LSB first, 128 bits)",
             (unsigned long long)bits_lo, (unsigned long long)bits_hi);
    char line[160];
    for (int base = 0; base < kBits; base += 16) {
        int n = snprintf(line, sizeof(line), "i=%3d", base);
        for (int i = 0; i < 16 && (base + i) < kBits; ++i) {
            n += snprintf(line + n, sizeof(line) - n, " %4d", adc_seq[base + i]);
        }
        ESP_LOGI("prbs_csv", "%s", line);
    }
    app_ir_ring_set(false);
}

/* Phosphor impulse-response capture.
 *
 * 1. Pad held in INPUT + pulldown so ADC1_CH4 is the only path.
 * 2. Tela: white disk of (rx, ry) at center → steady-state for `pre_ms`.
 * 3. Burst ADC sampling at 10 kHz: first quarter under WHITE, then we
 *    flip the FB to all-black and continue sampling — captures the
 *    optical decay through the ring + phosphor.
 * 4. CSV-style log on UART: one ESP_LOGI per pre/post phase, one per N
 *    samples to keep the queue happy. */
static void app_ir_decay_test(int trial_idx)
{
    const int kSamples = 256;
    const uint32_t kPeriodUs = 5000;    /* 5 ms/sample → 1.28 s post-switch window */
    const int kSwitchAt = kSamples / 4; /* 320 ms WHITE, 960 ms DECAY */
    static int buf[256];

    const float radius_mm = APP_RING_OD_MM * 0.5f;
    const int cx = APP_RING_CENTER_X;
    const int cy = APP_RING_CENTER_Y;
    const int rx = (int)(radius_mm * (float)s_fb.width / APP_CRT_VISIBLE_W_MM + 0.5f);
    const int ry = (int)(radius_mm * (float)s_fb.height / APP_CRT_VISIBLE_H_MM + 0.5f);

    /* Phase 1: white disk steady-state */
    app_fb_fill(&s_fb, 0);
    app_fb_fill_ellipse(&s_fb, cx, cy, rx, ry, 255);
    vTaskDelay(pdMS_TO_TICKS(80)); /* phosphor + IR-LED settle */

    /* Pad in INPUT + pulldown for ADC reads (no drain inside the loop) */
    gpio_set_direction(APP_IR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(APP_IR_PIN, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(500);

    /* Burst capture; flip FB to black mid-burst so the decay starts at
     * a known sample index. */
    for (int i = 0; i < kSamples; ++i) {
        adc_oneshot_read(s_ir_adc, APP_IR_ADC_CHANNEL, &buf[i]);
        if (i == kSwitchAt) {
            app_fb_fill(&s_fb, 0); /* impulse OFF */
        }
        esp_rom_delay_us(kPeriodUs);
    }

    /* Restore ring as the OUTPUT driver */
    app_ir_ring_set(false);

    /* Stats */
    int pre_min = 4095, pre_max = 0;
    long pre_sum = 0;
    for (int i = 0; i < kSwitchAt; ++i) {
        if (buf[i] < pre_min) pre_min = buf[i];
        if (buf[i] > pre_max) pre_max = buf[i];
        pre_sum += buf[i];
    }
    int post_min = 4095, post_max = 0;
    long post_sum = 0;
    for (int i = kSwitchAt; i < kSamples; ++i) {
        if (buf[i] < post_min) post_min = buf[i];
        if (buf[i] > post_max) post_max = buf[i];
        post_sum += buf[i];
    }
    ESP_LOGI("decay", "trial=%d period_us=%u switch_idx=%d N=%d", trial_idx,
             (unsigned)kPeriodUs, kSwitchAt, kSamples);
    ESP_LOGI("decay", "pre  (white): min=%4d max=%4d mean=%4ld", pre_min, pre_max,
             pre_sum / kSwitchAt);
    ESP_LOGI("decay", "post (decay): min=%4d max=%4d mean=%4ld", post_min, post_max,
             post_sum / (kSamples - kSwitchAt));

    /* Dump samples in chunks of 16 as CSV-ish lines for offline plotting.
     * Each line: "decay_csv idx0 v0 v1 ... v15". */
    char line[160];
    for (int base = 0; base < kSamples; base += 16) {
        int n = snprintf(line, sizeof(line), "i=%3d", base);
        for (int i = 0; i < 16 && (base + i) < kSamples; ++i) {
            n += snprintf(line + n, sizeof(line) - n, " %4d", buf[base + i]);
        }
        ESP_LOGI("decay_csv", "%s", line);
    }
}

static esp_err_t app_ir_ring_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = APP_IR_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_ir_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    err = adc_oneshot_config_channel(s_ir_adc, APP_IR_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
        return err;
    }
    app_ir_ring_set(false);
    return ESP_OK;
}

static void app_ir_ring_task(void *arg)
{
    (void)arg;
    app_ir_ring_calibrate();
    ESP_LOGI(TAG, "PRC: starting area sweep...");
    app_ir_area_sweep();
    ESP_LOGI(TAG, "PRC: starting 16x16 XY map...");
    app_ir_xy_map();
    ESP_LOGI(TAG, "PRC: starting decay tests (3 trials)...");
    for (int t = 0; t < 3; ++t) {
        app_ir_decay_test(t);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGI(TAG, "PRC: starting PRBS capture (64 bits)...");
    app_ir_prbs_capture();
    ESP_LOGI(TAG, "PRC: done — entering monitoring mode");
    app_draw_ring_indicator(&s_fb);

    /* MONITOR mode: ring stays OFF, the pad floats with internal pulldown
     * during sense so any external IR/photo source can pull it HIGH.
     * This is the closest analog to the validated devkit-v1 firmware. */
    s_ir_mode = APP_IR_MODE_MONITOR;
    bool stable_light = false;
    while (true) {
        const int v = app_ir_ring_measure();
        const int delta = v - s_ir_baseline;

        if (!stable_light && delta > s_ir_threshold + s_ir_hysteresis) {
            stable_light = true;
        } else if (stable_light && delta < s_ir_threshold - s_ir_hysteresis) {
            stable_light = false;
        }

        switch (s_ir_mode) {
        case APP_IR_MODE_AUTO:
            app_ir_ring_set(!stable_light);
            break;
        case APP_IR_MODE_FORCE_ON:
            app_ir_ring_set(true);
            break;
        case APP_IR_MODE_FORCE_OFF:
        case APP_IR_MODE_MONITOR:
            app_ir_ring_set(false);
            break;
        }

        ESP_LOGI("ir_ring", "ADC=%4d d=%+5d %s ring=%s", v, delta,
                 stable_light ? "[CLARO]" : "[ESCURO]", s_ir_ring_on ? "ON " : "OFF");

        vTaskDelay(pdMS_TO_TICKS(APP_IR_SAMPLE_MS));
    }
}

static esp_err_t app_start_core(crt_video_standard_t video_standard)
{
    crt_core_config_t config = {
        .video_standard = video_standard,
        .enable_color = k_enable_color || k_use_rgb332_framebuffer,
        .demo_pattern_mode = (k_enable_color || k_use_rgb332_framebuffer)
                                 ? CRT_DEMO_PATTERN_COLOR_BARS_RAMP
                                 : CRT_DEMO_PATTERN_LUMA_BARS,
        .target_ready_depth = 64,
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
        stimulus_config.pattern = CRT_STIMULUS_PATTERN_CHECKER;
        stimulus_config.cell_w = 8;
        stimulus_config.cell_h = 8;

        esp_err_t stimulus_err = crt_stimulus_init(&s_stimulus, &stimulus_config);
        if (stimulus_err != ESP_OK) {
            ESP_LOGE(TAG, "crt_stimulus_init failed: %s", esp_err_to_name(stimulus_err));
            return stimulus_err;
        }

        /* Draw the IR ring placement indicator into the indexed-8 surface
         * once. validated crt_fb_scanline_hook handles the hot path
         * (palette + I2S swap) with zero per-scanline cost. */
        app_draw_ring_indicator(&s_fb);

        crt_register_scanline_hook(crt_fb_scanline_hook, &s_fb);
        ESP_LOGI(TAG, "render: PRC ring placement indicator (%ux%u, OD %.0f mm, %d LEDs)",
                 APP_FB_WIDTH, APP_FB_HEIGHT, (double)APP_RING_OD_MM, APP_RING_LED_COUNT);
        ESP_LOGI(TAG, "PRC tip  : press the IR ring onto the markers, center crosshair = ring axis");
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

    /* Spawn the single-pin IR ring driver/sensor on Core 0 so it never
     * competes with the prep_task on Core 1. Failure is non-fatal — the
     * composite signal continues regardless. */
    if (app_ir_ring_init() == ESP_OK) {
        BaseType_t ok = xTaskCreatePinnedToCore(app_ir_ring_task, "ir_ring", 4096, NULL,
                                                tskIDLE_PRIORITY + 2, NULL, 0);
        if (ok != pdPASS) {
            ESP_LOGW(TAG, "ir_ring task spawn failed");
        }
    } else {
        ESP_LOGW(TAG, "ir_ring init failed — IR sensor disabled");
    }

#if CONFIG_CRT_TEST_STANDARD_TOGGLE
    app_run_standard_toggle_loop(video_standard);
#else
    app_run_diag_loop();
#endif
}
