#include "crt_core.h"

#include <string.h>

#include "esp_cpu.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "crt_diag.h"
#include "crt_demo_pattern.h"
#include "crt_hal.h"
#include "crt_line_policy.h"
#include "crt_stage.h"
#include "crt_waveform.h"

#define CRT_CORE_SYNC_LEVEL         ((uint16_t)0x0000)
#define CRT_CORE_BLANK_LEVEL        ((uint16_t)(23U << 8))
#define CRT_CORE_PREP_TASK_STACK    4096
#define CRT_CORE_PREP_TASK_PRIORITY 20
#define CRT_CORE_STAGE_COUNT        4
#define CRT_CORE_MAX_BURST_WIDTH    64
#define CRT_CORE_MAX_LINE_SAMPLES   1136

static bool s_initialized;
static bool s_running;
static crt_core_config_t s_config;
static crt_timing_profile_t s_timing;
static TaskHandle_t s_prep_task;
static uint32_t s_next_line_index;
static crt_demo_pattern_runtime_t s_demo_pattern;

typedef struct {
    crt_stage_fn_t fn;
    void *user_ctx;
} crt_core_stage_entry_t;

typedef struct {
    uint16_t ntsc[CRT_CORE_MAX_BURST_WIDTH];
    uint16_t pal_phase_a[CRT_CORE_MAX_BURST_WIDTH];
    uint16_t pal_phase_b[CRT_CORE_MAX_BURST_WIDTH];
} crt_core_burst_state_t;

static crt_core_stage_entry_t s_stages[CRT_CORE_STAGE_COUNT];
static crt_core_burst_state_t s_burst_state;
static bool s_fast_mono_mode;
static uint16_t s_fast_active_line[CRT_CORE_MAX_LINE_SAMPLES];
static uint16_t s_fast_blank_line[CRT_CORE_MAX_LINE_SAMPLES];
static uint16_t s_fast_vsync_line[CRT_CORE_MAX_LINE_SAMPLES];

static void crt_core_blanking_stage(const crt_line_context_t *ctx, crt_line_buffer_t *line, void *user_ctx)
{
    (void)ctx;
    (void)user_ctx;

    for (size_t i = 0; i < line->sample_count; ++i) {
        line->samples[i] = CRT_CORE_BLANK_LEVEL;
    }
}

static void crt_core_sync_stage(const crt_line_context_t *ctx, crt_line_buffer_t *line, void *user_ctx)
{
    size_t sync_width = crt_line_policy_sync_width((const crt_timing_profile_t *)user_ctx, ctx->line_type);

    for (size_t i = 0; i < sync_width && i < line->sample_count; ++i) {
        line->samples[i] = CRT_CORE_SYNC_LEVEL;
    }
}

static void crt_core_burst_stage(const crt_line_context_t *ctx, crt_line_buffer_t *line, void *user_ctx)
{
    size_t burst_width;
    const crt_core_burst_state_t *burst_state = (const crt_core_burst_state_t *)user_ctx;
    const uint16_t *template = burst_state->ntsc;

    if (!s_config.enable_color ||
        !crt_line_policy_has_burst(ctx->line_type) ||
        ctx->burst_width == 0 ||
        ctx->burst_offset >= line->sample_count) {
        return;
    }

    burst_width = ctx->burst_width;
    if ((size_t)ctx->burst_offset + burst_width > line->sample_count) {
        burst_width = line->sample_count - ctx->burst_offset;
    }

    if (ctx->video_standard == CRT_VIDEO_STANDARD_PAL) {
        /* Match the legacy phase alternation: line 0 starts with the inverted template. */
        template = ((ctx->line_index & 0x1U) == 0U) ? burst_state->pal_phase_b : burst_state->pal_phase_a;
    }

    memcpy(&line->samples[ctx->burst_offset], template, burst_width * sizeof(line->samples[0]));
}

static void crt_core_active_window_base_stage(const crt_line_context_t *ctx, crt_line_buffer_t *line, void *user_ctx)
{
    const crt_demo_pattern_runtime_t *demo_pattern = (const crt_demo_pattern_runtime_t *)user_ctx;
    const crt_demo_pattern_render_context_t demo_ctx = {
        .video_standard = ctx->video_standard,
        .line_index = ctx->line_index,
        .active_line_index = ctx->active_line_index,
        .in_active = ctx->in_active,
    };

    if (!ctx->in_active || ctx->active_offset >= line->sample_count) {
        return;
    }

    crt_demo_pattern_render_active_window(
        demo_pattern,
        &demo_ctx,
        CRT_CORE_BLANK_LEVEL,
        &line->samples[ctx->active_offset],
        line->sample_count - ctx->active_offset < ctx->active_width ? line->sample_count - ctx->active_offset : ctx->active_width);
}

static esp_err_t crt_core_init_builtin_stages(void)
{
    ESP_RETURN_ON_FALSE(s_timing.burst_width <= CRT_CORE_MAX_BURST_WIDTH,
                        ESP_ERR_INVALID_SIZE,
                        "crt_core",
                        "burst width exceeds static template size");

    crt_waveform_fill_ntsc_burst_template(s_burst_state.ntsc, s_timing.burst_width, CRT_CORE_BLANK_LEVEL);
    crt_waveform_fill_pal_burst_template(s_burst_state.pal_phase_a, s_timing.burst_width, CRT_CORE_BLANK_LEVEL, false);
    crt_waveform_fill_pal_burst_template(s_burst_state.pal_phase_b, s_timing.burst_width, CRT_CORE_BLANK_LEVEL, true);

    s_stages[0] = (crt_core_stage_entry_t) {
        .fn = crt_core_blanking_stage,
        .user_ctx = NULL,
    };
    s_stages[1] = (crt_core_stage_entry_t) {
        .fn = crt_core_sync_stage,
        .user_ctx = &s_timing,
    };
    s_stages[2] = (crt_core_stage_entry_t) {
        .fn = crt_core_burst_stage,
        .user_ctx = &s_burst_state,
    };
    s_stages[3] = (crt_core_stage_entry_t) {
        .fn = crt_core_active_window_base_stage,
        .user_ctx = &s_demo_pattern,
    };

    return ESP_OK;
}

static crt_line_context_t crt_core_build_line_context(uint32_t line_index)
{
    uint16_t active_line_index = 0;
    crt_timing_line_type_t line_type =
        crt_timing_get_line_type(s_config.video_standard, (uint16_t)(line_index % s_timing.total_lines));

    if (line_type == CRT_TIMING_LINE_TYPE_ACTIVE) {
        switch (s_config.video_standard) {
        case CRT_VIDEO_STANDARD_NTSC:
            active_line_index = (uint16_t)(line_index % s_timing.total_lines);
            break;
        case CRT_VIDEO_STANDARD_PAL:
            active_line_index = (uint16_t)((line_index % s_timing.total_lines) - 32U);
            break;
        default:
            active_line_index = 0;
            break;
        }
    }

    return (crt_line_context_t) {
        .video_standard = s_config.video_standard,
        .line_type = line_type,
        .line_index = (uint16_t)(line_index % s_timing.total_lines),
        .total_lines = s_timing.total_lines,
        .active_line_index = active_line_index,
        .active_line_count = s_timing.active_lines,
        .active_offset = s_timing.active_offset,
        .active_width = s_timing.active_width,
        .burst_offset = s_timing.burst_offset,
        .burst_width = s_timing.burst_width,
        .in_vblank = (line_type != CRT_TIMING_LINE_TYPE_ACTIVE),
        .in_active = (line_type == CRT_TIMING_LINE_TYPE_ACTIVE),
    };
}

static void crt_core_execute_stages(const crt_line_context_t *ctx, crt_line_buffer_t *line, bool render_active_stage)
{
    for (size_t i = 0; i < CRT_CORE_STAGE_COUNT; ++i) {
        if (!render_active_stage && i == (CRT_CORE_STAGE_COUNT - 1U)) {
            continue;
        }
        s_stages[i].fn(ctx, line, s_stages[i].user_ctx);
    }
}

static void crt_core_apply_i2s_word_swap(uint16_t *buffer, size_t sample_count)
{
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        uint16_t tmp = buffer[i];
        buffer[i] = buffer[i + 1];
        buffer[i + 1] = tmp;
    }
}

static void crt_core_fill_line(uint32_t line_index, uint16_t *buffer, size_t sample_count, bool render_active_stage)
{
    crt_line_context_t ctx = crt_core_build_line_context(line_index);
    crt_line_buffer_t line = {
        .samples = buffer,
        .sample_count = sample_count,
    };

    crt_core_execute_stages(&ctx, &line, render_active_stage);
    crt_core_apply_i2s_word_swap(buffer, sample_count);
}

static void crt_core_maybe_init_fast_mono_templates(void)
{
    if (s_fast_mono_mode || s_timing.samples_per_line > CRT_CORE_MAX_LINE_SAMPLES) {
        return;
    }

    if (s_config.demo_pattern_mode == CRT_DEMO_PATTERN_DISABLED) {
        return;
    }

    crt_core_fill_line(0, s_fast_active_line, s_timing.samples_per_line, true);
    crt_core_fill_line(s_timing.active_lines, s_fast_blank_line, s_timing.samples_per_line, true);
    crt_core_fill_line(s_timing.active_lines + 3U, s_fast_vsync_line, s_timing.samples_per_line, true);
    s_fast_mono_mode = true;
}

static esp_err_t crt_core_fill_slot(size_t slot_index)
{
    uint16_t *buffer = NULL;
    esp_err_t err = crt_hal_get_line_buffer(slot_index, &buffer);
    esp_cpu_cycle_count_t prep_start;
    bool render_active_stage = crt_hal_get_recycled_queue_depth() >= s_config.min_ready_depth;

    ESP_RETURN_ON_ERROR(err, "crt_core", "failed to get line buffer");
    prep_start = esp_cpu_get_cycle_count();
    if (s_fast_mono_mode) {
        crt_timing_line_type_t line_type =
            crt_timing_get_line_type(s_config.video_standard, (uint16_t)(s_next_line_index % s_timing.total_lines));
        const uint16_t *src = s_fast_blank_line;

        if (line_type == CRT_TIMING_LINE_TYPE_ACTIVE) {
            src = s_fast_active_line;
        } else if (line_type == CRT_TIMING_LINE_TYPE_VSYNC) {
            src = s_fast_vsync_line;
        }
        memcpy(buffer, src, s_timing.samples_per_line * sizeof(uint16_t));
    } else {
        crt_core_fill_line(s_next_line_index, buffer, s_timing.samples_per_line, render_active_stage);
    }
    crt_diag_update_prep_cycles((uint32_t)(esp_cpu_get_cycle_count() - prep_start));
    s_next_line_index = (s_next_line_index + 1) % s_timing.total_lines;
    return ESP_OK;
}

static void crt_core_prep_task(void *arg)
{
    size_t slot_index;

    (void)arg;
    while (true) {
        if (crt_hal_wait_recycled_slot(&slot_index, (uint32_t)portMAX_DELAY) != ESP_OK) {
            continue;
        }

        if (!s_running) {
            continue;
        }

        crt_diag_update_ready_queue_depth((uint32_t)crt_hal_get_recycled_queue_depth());
        crt_diag_set_dma_underrun_count(crt_hal_get_dma_underrun_count());
        crt_core_fill_slot(slot_index);
    }
}

esp_err_t crt_core_init(const crt_core_config_t *config)
{
    crt_hal_config_t hal_config;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, "crt_core", "config is null");
    ESP_RETURN_ON_FALSE(config->prep_task_core >= 0 && config->prep_task_core < portNUM_PROCESSORS,
                        ESP_ERR_INVALID_ARG, "crt_core", "prep task core must be a valid CPU index");

    err = crt_timing_get_profile(config->video_standard, &s_timing);
    ESP_RETURN_ON_ERROR(err, "crt_core", "timing profile lookup failed");

    memset(&hal_config, 0, sizeof(hal_config));
    hal_config.sample_rate_hz = s_timing.sample_rate_hz;
    hal_config.dma_line_count = config->target_ready_depth;
    hal_config.dma_samples_per_line = s_timing.samples_per_line;
    hal_config.video_gpio_num = 25;

    err = crt_hal_init(&hal_config);
    ESP_RETURN_ON_ERROR(err, "crt_core", "hal init failed");

    s_config = *config;
    s_fast_mono_mode = false;
    crt_diag_reset();
    crt_demo_pattern_runtime_init(&s_demo_pattern, s_config.demo_pattern_mode, s_timing.active_lines);
    ESP_RETURN_ON_ERROR(crt_core_init_builtin_stages(), "crt_core", "builtin stage init failed");
    crt_core_maybe_init_fast_mono_templates();
    s_initialized = true;
    return ESP_OK;
}

esp_err_t crt_core_start(void)
{
    esp_err_t err;

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "crt_core", "not initialized");
    ESP_RETURN_ON_FALSE(!s_running, ESP_ERR_INVALID_STATE, "crt_core", "already running");

    s_next_line_index = 0;
    for (size_t slot_index = 0; slot_index < crt_hal_get_slot_count(); ++slot_index) {
        err = crt_core_fill_slot(slot_index);
        ESP_RETURN_ON_ERROR(err, "crt_core", "failed to prime line buffers");
    }
    crt_diag_update_ready_queue_depth((uint32_t)crt_hal_get_recycled_queue_depth());
    crt_diag_set_dma_underrun_count(crt_hal_get_dma_underrun_count());

    if (s_prep_task == NULL) {
        BaseType_t task_ok = xTaskCreatePinnedToCore(
            crt_core_prep_task,
            "crt_core_prep",
            CRT_CORE_PREP_TASK_STACK,
            NULL,
            CRT_CORE_PREP_TASK_PRIORITY,
            &s_prep_task,
            s_config.prep_task_core);
        ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, "crt_core", "failed to create prep task");
    }

    err = crt_hal_start();
    if (err != ESP_OK) {
        vTaskDelete(s_prep_task);
        s_prep_task = NULL;
        return err;
    }

    s_running = true;
    return ESP_OK;
}

esp_err_t crt_core_stop(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "crt_core", "not initialized");
    ESP_RETURN_ON_FALSE(s_running, ESP_ERR_INVALID_STATE, "crt_core", "not running");

    s_running = false;
    ESP_RETURN_ON_ERROR(crt_hal_stop(), "crt_core", "hal stop failed");

    if (s_prep_task != NULL) {
        vTaskDelete(s_prep_task);
        s_prep_task = NULL;
    }

    return ESP_OK;
}

esp_err_t crt_core_deinit(void)
{
    esp_err_t err;

    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "crt_core", "not initialized");

    if (s_running) {
        ESP_RETURN_ON_ERROR(crt_core_stop(), "crt_core", "core stop failed");
    }

    err = crt_hal_shutdown();
    ESP_RETURN_ON_ERROR(err, "crt_core", "hal shutdown failed");

    s_initialized = false;
    memset(&s_timing, 0, sizeof(s_timing));
    memset(&s_config, 0, sizeof(s_config));
    return ESP_OK;
}

esp_err_t crt_core_get_diag_snapshot(crt_diag_snapshot_t *out_snapshot)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, "crt_core", "not initialized");
    ESP_RETURN_ON_FALSE(out_snapshot != NULL, ESP_ERR_INVALID_ARG, "crt_core", "snapshot is null");

    crt_diag_get_snapshot(out_snapshot);
    return ESP_OK;
}
