/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Custom ESP32-S3 Board Driver for:
 * - ST7789 240x240 SPI LCD (Pins: SCL=12, SDA=11, RES=10, DC=9, BLK=7, CS=-1)
 * - INMP441 I2S MEMS Microphone (Pins: SCK=6, WS=5, SD=4)
 * - MAX98357A I2S 3.2W Audio Amp (Pins: BCLK=16, LRC=17, DIN=15)
 * - Push-to-Talk BOOT Button (GPIO 0)
 */

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"
#include "muse_ui.h"
#include "muse_voice.h"
#include "muse_state.h"
#include "muse_settings.h"

#ifndef MUSE_AUDIO_RATE
#define MUSE_AUDIO_RATE 16000
#endif

#ifndef MUSE_UI_CORE
#define MUSE_UI_CORE 1
#endif

#ifndef MUSE_UI_PRIORITY
#define MUSE_UI_PRIORITY 5
#endif

static const char *TAG = "custom_s3_st7789";

#define LCD_HOST SPI2_HOST
#define LCD_RES  240
#define LCD_SCLK GPIO_NUM_12
#define LCD_MOSI GPIO_NUM_11
#define LCD_DC   GPIO_NUM_9
#define LCD_RST  GPIO_NUM_10
#define LCD_BL   GPIO_NUM_7
#define LCD_CS   GPIO_NUM_NC   /* -1, tied to GND */

#define DRAW_BUF_LINES 20

/* MAX98357A I2S Speaker on I2S_NUM_0 */
#define SPK_I2S_NUM   I2S_NUM_0
#define SPK_I2S_BCLK  GPIO_NUM_16
#define SPK_I2S_LRC   GPIO_NUM_17
#define SPK_I2S_DIN   GPIO_NUM_15

/* INMP441 I2S Mic on I2S_NUM_1 */
#define MIC_I2S_NUM   I2S_NUM_1
#define MIC_I2S_SCK   GPIO_NUM_6
#define MIC_I2S_WS    GPIO_NUM_5
#define MIC_I2S_SD    GPIO_NUM_4

#define TALK_GPIO     GPIO_NUM_0

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static muse_gpio_button_t s_talk;

static i2s_chan_handle_t s_spk_chan = NULL;
static i2s_chan_handle_t s_mic_chan = NULL;

static int s_mic_gain_q8 = 1144; /* 25 dB default */
static int32_t s_mic_dc[2] = {0, 0};

static esp_err_t init(void)
{
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_talk, TALK_GPIO), TAG, "talk button");
    return ESP_OK;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    *touch = NULL;

    /* Backlight: configure as direct GPIO output and turn ON */
    gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << LCD_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL, 1);

    /* Hardware reset sequence (matching working Arduino driver) */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    /* SPI bus initialization */
    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_RES * LCD_RES * 2,
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "failed spi_bus_initialize");
        return NULL;
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 3,                 /* ST7789 without CS pin requires Mode 3 */
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_io) != ESP_OK) {
        ESP_LOGE(TAG, "failed esp_lcd_new_panel_io_spi");
        return NULL;
    }

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "failed esp_lcd_new_panel_st7789");
        return NULL;
    }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_disp_on_off(s_panel, true);

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_core_id = MUSE_UI_CORE;
    adapter_cfg.task_priority = MUSE_UI_PRIORITY;
    if (esp_lv_adapter_init(&adapter_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed esp_lv_adapter_init");
        return NULL;
    }

    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel,
        .panel_io = s_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = LCD_RES,
            .ver_res = LCD_RES,
            .buffer_height = DRAW_BUF_LINES,
            .use_psram = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "failed esp_lv_adapter_register_display");
        return NULL;
    }

    if (esp_lv_adapter_start() != ESP_OK) {
        ESP_LOGE(TAG, "failed esp_lv_adapter_start");
        return NULL;
    }

    return disp;
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static void set_brightness(int pct)
{
    gpio_set_level(LCD_BL, pct > 0 ? 1 : 0);
}

static void panel_sleep(bool sleep)
{
    esp_lcd_panel_disp_sleep(s_panel, sleep);
}

/* Audio interface implementation */
static int data_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h; (void)type; (void)on;
    return ESP_CODEC_DEV_OK;
}

static int spk_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    int16_t *s = (int16_t *)data;
    int count = size / sizeof(int16_t);
    int vol = muse_settings_volume(); // 0..100
    // Clean digital volume boost: 1.8x at max volume with headroom limiting to eliminate bursting noise
    int32_t mult = (vol * 18); // /1000
    for (int i = 0; i < count; i++) {
        int32_t v = ((int32_t)s[i] * mult) / 1000;
        if (v > 28000) v = 28000;
        else if (v < -28000) v = -28000;
        s[i] = (int16_t)v;
    }
    size_t wrote;
    return i2s_channel_write(s_spk_chan, (uint8_t *)s, size, &wrote, portMAX_DELAY) == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int mic_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    size_t got;
    if (i2s_channel_read(s_mic_chan, data, size, &got, pdMS_TO_TICKS(1000)) != ESP_OK || got != (size_t)size) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    int16_t *s = (int16_t *)data;
    for (int i = 0; i < size / 2; i++) {
        int32_t *dc = &s_mic_dc[i & 1];
        *dc += (s[i] * 256 - *dc) >> 8;
        int v = (s[i] - (*dc >> 8)) * s_mic_gain_q8 >> 8;
        s[i] = v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : v;
    }
    return ESP_CODEC_DEV_OK;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    (void)mic;
    s_mic_gain_q8 = (int)(256.0f * powf(10.0f, (db - 12.0f) / 20.0f));
}

static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    /* TX (Speaker) */
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(SPK_I2S_NUM, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&tx_chan_cfg, &s_spk_chan, NULL), TAG, "spk chan");

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_I2S_BCLK,
            .ws = SPK_I2S_LRC,
            .dout = SPK_I2S_DIN,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_spk_chan, &tx_std_cfg), TAG, "spk std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_spk_chan), TAG, "spk on");

    /* RX (Mic) */
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_NUM, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&rx_chan_cfg, NULL, &s_mic_chan), TAG, "mic chan");

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_I2S_SCK,
            .ws = MIC_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_I2S_SD,
        },
    };
    rx_std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    rx_std_cfg.slot_cfg.ws_width = 32;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_mic_chan, &rx_std_cfg), TAG, "mic std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_mic_chan), TAG, "mic on");

    static const audio_codec_data_if_t spk_if = { .enable = data_enable, .write = spk_write };
    static const audio_codec_data_if_t mic_if = { .enable = data_enable, .read = mic_read };
    esp_codec_dev_cfg_t out_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .data_if = &spk_if };
    esp_codec_dev_cfg_t in_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_IN, .data_if = &mic_if };
    *spk = esp_codec_dev_new(&out_cfg);
    *mic = esp_codec_dev_new(&in_cfg);

    muse_settings_set_volume(100);
    muse_settings_set_mic_gain(25);

    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_talk);
}

static const muse_board_t s_board = {
    .name = "Custom ESP32-S3 ST7789",
    .width = LCD_RES,
    .height = LCD_RES,
    .round = false,
    .no_ring = true,
    .touch = false,
    .diagonal_in = 1.3f,
    .talk_button = "boot",
    .aux_button = "boot",
    .talk_hint = { LV_ALIGN_BOTTOM_RIGHT, -12, -8 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .audio_init = audio_init,
    .mic_slot = 0,
    .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons,
};

const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
