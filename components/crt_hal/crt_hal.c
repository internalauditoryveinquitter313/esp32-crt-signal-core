#include "crt_hal.h"

#include "crt_hal_clock.h"

#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_private/esp_clk.h"
#include "esp_private/i2s_platform.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal/clk_tree_ll.h"
#include "hal/dac_ll.h"
#include "hal/i2s_hal.h"
#include "hal/i2s_ll.h"
#include "hal/i2s_types.h"
#include "soc/i2s_periph.h"
#include "soc/lldesc.h"
#include "soc/rtc.h"

#include <inttypes.h>
#include <string.h>

#include "clk_ctrl_os.h"
#include "rom/lldesc.h"

#define CRT_HAL_I2S_NUM         0
#define CRT_HAL_I2S_BIT_WIDTH   16
#define CRT_HAL_SYNC_LEVEL      ((uint16_t)0x0000)
#define CRT_HAL_BLANK_LEVEL     ((uint16_t)(23U << 8))
#define CRT_HAL_DESC_ALLOC_CAPS (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
#define CRT_HAL_INTR_FLAGS (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_LOWMED)

typedef struct {
    crt_hal_config_t config;
    i2s_dev_t *dev;
    intr_handle_t intr_handle;
    lldesc_t *descs;
    uint16_t **line_buffers;
    QueueHandle_t refill_queue;
    portMUX_TYPE dac_lock;
    volatile uint32_t dma_underrun_count;
    bool initialized;
    bool running;
    bool use_apll;
} crt_hal_state_t;

static crt_hal_state_t s_hal = {
    .dac_lock = portMUX_INITIALIZER_UNLOCKED,
};

static const char *TAG = "crt_hal";

static uint32_t crt_hal_set_apll_freq(uint32_t mclk_hz)
{
    uint32_t real_freq = 0;
    uint32_t o_div, sdm0, sdm1, sdm2;
    crt_hal_apll_coeff_t coeffs;

    /*
     * Use rtc_clk_apll_coeff_calc for the target, then log and compare.
     * ESP_8_BIT's proven coefficients are then selected by target
     * sample rate so PAL does not accidentally run on the NTSC APLL.
     */
    real_freq = rtc_clk_apll_coeff_calc(mclk_hz, &o_div, &sdm0, &sdm1, &sdm2);
    ESP_LOGI(TAG,
             "APLL calc: target=%" PRIu32 " real=%" PRIu32 " o_div=%" PRIu32
             " sdm=[%02x,%02x,%02x]",
             mclk_hz, real_freq, o_div, (unsigned)sdm0, (unsigned)sdm1, (unsigned)sdm2);

    if (!crt_hal_apll_coeffs_for_sample_rate(mclk_hz, &coeffs)) {
        ESP_LOGE(TAG, "unsupported APLL sample rate: %" PRIu32, mclk_hz);
        return 0;
    }

    rtc_clk_apll_coeff_set(coeffs.o_div, coeffs.sdm0, coeffs.sdm1, coeffs.sdm2);
    ESP_LOGI(TAG,
             "APLL override: %s sample=%" PRIu32 " apll=%" PRIu32 " o_div=%" PRIu32
             " sdm=[%02x,%02x,%02x]",
             coeffs.name, coeffs.sample_rate_hz, coeffs.apll_hz, coeffs.o_div,
             (unsigned)coeffs.sdm0, (unsigned)coeffs.sdm1, (unsigned)coeffs.sdm2);
    return coeffs.apll_hz;
}

static esp_err_t crt_hal_configure_clock(uint32_t sample_rate_hz, bool use_apll)
{
    if (use_apll) {
        uint32_t apll_hz = crt_hal_set_apll_freq(sample_rate_hz);
        ESP_RETURN_ON_FALSE(apll_hz != 0, ESP_ERR_INVALID_ARG, TAG, "failed to set APLL");
        i2s_ll_tx_clk_set_src(s_hal.dev, I2S_CLK_SRC_APLL);
        i2s_ll_set_raw_mclk_div(s_hal.dev, 1, 1, 0);
        i2s_ll_tx_set_bck_div_num(s_hal.dev, 1);
        return ESP_OK;
    } else {
        uint32_t src_clk_hz = esp_clk_apb_freq() * 2;
        hal_utils_clk_div_t mclk_div = {};

        ESP_RETURN_ON_FALSE((float)src_clk_hz / (float)sample_rate_hz > 1.99f, ESP_ERR_INVALID_ARG,
                            TAG, "mclk divider below minimum");
        ESP_RETURN_ON_FALSE((src_clk_hz / sample_rate_hz) < 256, ESP_ERR_INVALID_ARG, TAG,
                            "mclk divider above maximum");

        i2s_ll_tx_clk_set_src(s_hal.dev, I2S_CLK_SRC_DEFAULT);
        i2s_hal_calc_mclk_precise_division(src_clk_hz, sample_rate_hz, &mclk_div);
        i2s_ll_tx_set_mclk(s_hal.dev, &mclk_div);
        i2s_ll_tx_set_bck_div_num(s_hal.dev, 1);
        return ESP_OK;
    }
}

static int IRAM_ATTR crt_hal_find_desc_index(uint32_t desc_addr)
{
    if (s_hal.descs == NULL || s_hal.config.dma_line_count == 0) {
        return -1;
    }

    const uintptr_t base = (uintptr_t)s_hal.descs;
    const uintptr_t addr = (uintptr_t)desc_addr;
    const uintptr_t desc_size = sizeof(s_hal.descs[0]);

    if (addr < base) {
        return -1;
    }

    const uintptr_t offset = addr - base;
    if (offset % desc_size != 0) {
        return -1;
    }

    const size_t desc_index = offset / desc_size;
    return (desc_index < s_hal.config.dma_line_count) ? (int)desc_index : -1;
}

static void IRAM_ATTR crt_hal_isr(void *arg)
{
    uint32_t intr_status;
    uint32_t eof_desc;
    int desc_index;
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)arg;

    intr_status = i2s_ll_get_intr_status(s_hal.dev);
    if (intr_status == 0) {
        return;
    }
    i2s_ll_clear_intr_status(s_hal.dev, intr_status);

    if (intr_status & I2S_LL_EVENT_TX_EOF) {
        i2s_ll_tx_get_eof_des_addr(s_hal.dev, &eof_desc);
        desc_index = crt_hal_find_desc_index(eof_desc);
        if (desc_index >= 0) {
            uint32_t slot_index = (uint32_t)desc_index;
            if (xQueueSendFromISR(s_hal.refill_queue, &slot_index, &higher_priority_task_woken) !=
                pdTRUE) {
                s_hal.dma_underrun_count++;
            }
        }
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void crt_hal_enable_output(void)
{
    portENTER_CRITICAL(&s_hal.dac_lock);
    dac_ll_digi_enable_dma(true);
    dac_ll_rtc_sync_by_adc(false);
    dac_ll_power_on(DAC_CHAN_0);
    portEXIT_CRITICAL(&s_hal.dac_lock);
}

static void crt_hal_disable_output(void)
{
    portENTER_CRITICAL(&s_hal.dac_lock);
    dac_ll_digi_enable_dma(false);
    dac_ll_power_down(DAC_CHAN_0);
    portEXIT_CRITICAL(&s_hal.dac_lock);
}

static void crt_hal_free_dma_resources(void);

static esp_err_t crt_hal_alloc_dma_resources(void)
{
    size_t buffer_bytes = s_hal.config.dma_samples_per_line * sizeof(uint16_t);

    s_hal.descs =
        heap_caps_calloc(s_hal.config.dma_line_count, sizeof(lldesc_t), CRT_HAL_DESC_ALLOC_CAPS);
    ESP_RETURN_ON_FALSE(s_hal.descs != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate descriptors");

    s_hal.line_buffers = (uint16_t **)heap_caps_calloc(s_hal.config.dma_line_count,
                                                       sizeof(uint16_t *), CRT_HAL_DESC_ALLOC_CAPS);
    ESP_RETURN_ON_FALSE(s_hal.line_buffers != NULL, ESP_ERR_NO_MEM, TAG,
                        "failed to allocate buffer list");

    for (size_t i = 0; i < s_hal.config.dma_line_count; ++i) {
        s_hal.line_buffers[i] = heap_caps_calloc(1, buffer_bytes, CRT_HAL_DESC_ALLOC_CAPS);
        if (s_hal.line_buffers[i] == NULL) {
            ESP_LOGE(TAG, "failed to allocate line buffer %u", (unsigned)i);
            crt_hal_free_dma_resources();
            return ESP_ERR_NO_MEM;
        }

        lldesc_config(&s_hal.descs[i], 1, 1, 0, buffer_bytes);
        s_hal.descs[i].size = buffer_bytes;
        s_hal.descs[i].length = buffer_bytes;
        s_hal.descs[i].buf = (const uint8_t *)s_hal.line_buffers[i];
        s_hal.descs[i].offset = 0;
        s_hal.descs[i].qe.stqe_next = &s_hal.descs[(i + 1) % s_hal.config.dma_line_count];
    }

    return ESP_OK;
}

static void crt_hal_free_dma_resources(void)
{
    if (s_hal.line_buffers != NULL) {
        for (size_t i = 0; i < s_hal.config.dma_line_count; ++i) {
            free(s_hal.line_buffers[i]);
            s_hal.line_buffers[i] = NULL;
        }
        free((void *)s_hal.line_buffers);
        s_hal.line_buffers = NULL;
    }

    free(s_hal.descs);
    s_hal.descs = NULL;
}

esp_err_t crt_hal_init(const crt_hal_config_t *config)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(config->video_gpio_num == 25, ESP_ERR_INVALID_ARG, TAG,
                        "GPIO25 is required for ESP32 DAC1");
    ESP_RETURN_ON_FALSE(config->dma_line_count >= 2, ESP_ERR_INVALID_ARG, TAG,
                        "at least two DMA lines required");
    ESP_RETURN_ON_FALSE(config->dma_samples_per_line > 0, ESP_ERR_INVALID_ARG, TAG,
                        "invalid samples per line");
    ESP_RETURN_ON_FALSE(!s_hal.initialized, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    memset(&s_hal, 0, sizeof(s_hal));
    s_hal.dac_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    s_hal.config = *config;
    s_hal.dev = I2S_LL_GET_HW(CRT_HAL_I2S_NUM);
    s_hal.use_apll = true;

    ESP_RETURN_ON_ERROR(i2s_platform_acquire_occupation(I2S_CTLR_HP, CRT_HAL_I2S_NUM, "crt_hal"),
                        TAG, "failed to acquire I2S0");

    s_hal.refill_queue = xQueueCreate((UBaseType_t)config->dma_line_count, sizeof(uint32_t));
    ESP_GOTO_ON_FALSE(s_hal.refill_queue != NULL, ESP_ERR_NO_MEM, err_release, TAG,
                      "failed to create refill queue");

    ESP_GOTO_ON_ERROR(crt_hal_alloc_dma_resources(), err_release, TAG,
                      "failed to allocate dma resources");

    if (s_hal.use_apll) {
        periph_rtc_apll_acquire();
    }

    ESP_GOTO_ON_ERROR(crt_hal_configure_clock(config->sample_rate_hz, s_hal.use_apll), err_apll,
                      TAG, "failed to configure clock");

    i2s_ll_enable_builtin_adc_dac(s_hal.dev, true);
    i2s_ll_tx_reset(s_hal.dev);
    i2s_ll_tx_reset_dma(s_hal.dev);
    i2s_ll_tx_reset_fifo(s_hal.dev);
    i2s_ll_tx_set_slave_mod(s_hal.dev, false);
    i2s_ll_tx_set_sample_bit(s_hal.dev, CRT_HAL_I2S_BIT_WIDTH, CRT_HAL_I2S_BIT_WIDTH);
    i2s_ll_tx_enable_mono_mode(s_hal.dev, true);
    s_hal.dev->conf.tx_mono = 1;
    i2s_ll_tx_select_std_slot(s_hal.dev, I2S_STD_SLOT_BOTH, true);
    i2s_ll_tx_enable_msb_shift(s_hal.dev, false);
    i2s_ll_tx_set_ws_width(s_hal.dev, CRT_HAL_I2S_BIT_WIDTH);
    i2s_ll_tx_enable_msb_right(s_hal.dev, false);
    i2s_ll_tx_enable_right_first(s_hal.dev, true);
    i2s_ll_tx_force_enable_fifo_mod(s_hal.dev, true);
    i2s_ll_dma_enable_auto_write_back(s_hal.dev, true);
    i2s_ll_dma_enable_eof_on_fifo_empty(s_hal.dev, true);
    i2s_ll_enable_intr(s_hal.dev, I2S_LL_EVENT_TX_EOF | I2S_LL_EVENT_TX_TEOF, true);

    ESP_GOTO_ON_ERROR(esp_intr_alloc(i2s_periph_signal[CRT_HAL_I2S_NUM].irq, CRT_HAL_INTR_FLAGS,
                                     crt_hal_isr, NULL, &s_hal.intr_handle),
                      err_apll, TAG, "failed to allocate interrupt");

    /* Match the ESP-IDF DAC driver's RTC pad bring-up before routing digital DAC output. */
    rtc_gpio_init((gpio_num_t)config->video_gpio_num);
    rtc_gpio_set_direction((gpio_num_t)config->video_gpio_num, RTC_GPIO_MODE_DISABLED);
    rtc_gpio_pullup_dis((gpio_num_t)config->video_gpio_num);
    rtc_gpio_pulldown_dis((gpio_num_t)config->video_gpio_num);

    ESP_LOGI(TAG, "APLL use_apll=%d", s_hal.use_apll);
    ESP_LOGI(TAG,
             "I2S config: tx_bits_mod=%d tx_fifo_mod=%d tx_chan_mod=%d tx_mono=%d lcd_en=%d "
             "tx_right_first=%d fifo_mod_force_en=%d",
             s_hal.dev->sample_rate_conf.tx_bits_mod, s_hal.dev->fifo_conf.tx_fifo_mod,
             s_hal.dev->conf_chan.tx_chan_mod, s_hal.dev->conf.tx_mono, s_hal.dev->conf2.lcd_en,
             s_hal.dev->conf.tx_right_first, s_hal.dev->fifo_conf.tx_fifo_mod_force_en);
    s_hal.initialized = true;
    return ESP_OK;

err_apll:
    if (s_hal.use_apll) {
        periph_rtc_apll_release();
    }
    crt_hal_free_dma_resources();
err_release:
    if (s_hal.refill_queue != NULL) {
        vQueueDelete(s_hal.refill_queue);
        s_hal.refill_queue = NULL;
    }
    i2s_platform_release_occupation(I2S_CTLR_HP, CRT_HAL_I2S_NUM);
    return ret;
}

esp_err_t crt_hal_start(void)
{
    ESP_RETURN_ON_FALSE(s_hal.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(!s_hal.running, ESP_ERR_INVALID_STATE, TAG, "already running");

    xQueueReset(s_hal.refill_queue);
    crt_hal_enable_output();
    esp_intr_enable(s_hal.intr_handle);
    i2s_ll_tx_start_link(s_hal.dev, (uint32_t)(uintptr_t)&s_hal.descs[0]);
    i2s_ll_enable_dma(s_hal.dev, true);
    i2s_ll_tx_enable_intr(s_hal.dev);
    i2s_ll_tx_start(s_hal.dev);
    s_hal.running = true;
    return ESP_OK;
}

esp_err_t crt_hal_stop(void)
{
    ESP_RETURN_ON_FALSE(s_hal.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(s_hal.running, ESP_ERR_INVALID_STATE, TAG, "not running");

    s_hal.running = false;
    esp_intr_disable(s_hal.intr_handle);
    i2s_ll_tx_stop(s_hal.dev);
    i2s_ll_tx_stop_link(s_hal.dev);
    i2s_ll_tx_disable_intr(s_hal.dev);
    i2s_ll_enable_dma(s_hal.dev, false);
    crt_hal_disable_output();
    return ESP_OK;
}

esp_err_t crt_hal_shutdown(void)
{
    ESP_RETURN_ON_FALSE(s_hal.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (s_hal.running) {
        ESP_RETURN_ON_ERROR(crt_hal_stop(), TAG, "failed to stop before shutdown");
    }
    if (s_hal.intr_handle != NULL) {
        ESP_RETURN_ON_ERROR(esp_intr_free(s_hal.intr_handle), TAG, "failed to free interrupt");
        s_hal.intr_handle = NULL;
    }

    i2s_ll_enable_intr(s_hal.dev, I2S_LL_EVENT_TX_EOF | I2S_LL_EVENT_TX_TEOF, false);
    i2s_ll_enable_builtin_adc_dac(s_hal.dev, false);
    rtc_gpio_deinit((gpio_num_t)s_hal.config.video_gpio_num);

    if (s_hal.use_apll) {
        periph_rtc_apll_release();
    }
    crt_hal_free_dma_resources();
    if (s_hal.refill_queue != NULL) {
        vQueueDelete(s_hal.refill_queue);
        s_hal.refill_queue = NULL;
    }
    ESP_RETURN_ON_ERROR(i2s_platform_release_occupation(I2S_CTLR_HP, CRT_HAL_I2S_NUM), TAG,
                        "failed to release I2S0");

    memset(&s_hal, 0, sizeof(s_hal));
    s_hal.dac_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return ESP_OK;
}

size_t crt_hal_get_slot_count(void)
{
    if (!s_hal.initialized) {
        return 0;
    }
    return s_hal.config.dma_line_count;
}

esp_err_t crt_hal_get_line_buffer(size_t slot_index, uint16_t **out_buffer)
{
    ESP_RETURN_ON_FALSE(s_hal.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(out_buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "out buffer is null");
    ESP_RETURN_ON_FALSE(slot_index < s_hal.config.dma_line_count, ESP_ERR_INVALID_ARG, TAG,
                        "slot out of range");

    *out_buffer = s_hal.line_buffers[slot_index];
    return ESP_OK;
}

esp_err_t crt_hal_wait_recycled_slot(size_t *out_slot_index, uint32_t ticks_to_wait)
{
    uint32_t slot_index = 0;

    ESP_RETURN_ON_FALSE(s_hal.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(out_slot_index != NULL, ESP_ERR_INVALID_ARG, TAG, "out slot is null");

    if (xQueueReceive(s_hal.refill_queue, &slot_index, (TickType_t)ticks_to_wait) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *out_slot_index = slot_index;
    return ESP_OK;
}

uint32_t crt_hal_get_dma_underrun_count(void)
{
    return s_hal.dma_underrun_count;
}

size_t crt_hal_get_recycled_queue_depth(void)
{
    if (!s_hal.initialized || s_hal.refill_queue == NULL) {
        return 0;
    }
    return (size_t)uxQueueMessagesWaiting(s_hal.refill_queue);
}
