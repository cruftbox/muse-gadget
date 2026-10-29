/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Waveshare ESP32-S3-Touch-LCD-1.54: square 240x240 ST7789 SPI LCD,
 * CST816S touch, ES8311 speaker DAC, ES7210 microphone ADC, three buttons,
 * battery measurement and a software-controlled power latch. Pin assignments
 * and bus settings follow Waveshare's ESP-IDF 5.5.1 factory BSP.
 */
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "es7210_adc.h"
#include "es8311_codec.h"
#include "muse_audio.h"
#include "muse_board.h"
#include "muse_mem.h"

static const char *TAG = "board";

#define LCD_RES 240
#define LCD_HOST SPI2_HOST
#define LCD_MOSI GPIO_NUM_39
#define LCD_SCLK GPIO_NUM_38
#define LCD_CS GPIO_NUM_21
#define LCD_DC GPIO_NUM_45
#define LCD_RST GPIO_NUM_40
#define LCD_BL GPIO_NUM_46
#define DRAW_BUF_LINES 40

#define I2C_SDA GPIO_NUM_42
#define I2C_SCL GPIO_NUM_41
#define TOUCH_INT GPIO_NUM_48
#define TOUCH_RST GPIO_NUM_47

#define I2S_MCLK GPIO_NUM_8
#define I2S_BCLK GPIO_NUM_9
#define I2S_WS GPIO_NUM_10
#define I2S_DIN GPIO_NUM_11
#define I2S_DOUT GPIO_NUM_12
#define PA_EN GPIO_NUM_7
#define I2S_DMA_DESC 4
#define I2S_DMA_FRAMES 160

#define PLUS_GPIO GPIO_NUM_4
#define POWER_GPIO GPIO_NUM_5
#define BOOT_GPIO GPIO_NUM_0
#define POWER_HOLD_US 1500000

#define BATT_ADC ADC_CHANNEL_0 /* GPIO1, through a 200K/100K divider */
#define POWER_EN GPIO_NUM_2
#define CHARGE_GPIO GPIO_NUM_3 /* active low */

static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_touch_handle_t s_touch;
static muse_gpio_button_t s_talk, s_aux, s_power;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_power_armed;
static bool s_power_long;
static int64_t s_power_held_at;

static esp_err_t init(void)
{
    /* GPIO2 holds the battery rail on after the PWR button is released. */
    const gpio_config_t power = {
        .pin_bit_mask = 1ULL << POWER_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&power), TAG, "power enable");
    ESP_RETURN_ON_ERROR(gpio_set_level(POWER_EN, 1), TAG, "power on");

    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c), TAG, "i2c");

    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_talk, PLUS_GPIO), TAG, "+/key button");
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_aux, BOOT_GPIO), TAG, "boot/- button");
    ESP_RETURN_ON_ERROR(muse_gpio_button_init(&s_power, POWER_GPIO), TAG, "power button");
    /* PWR may still be held after boot. Swallow that press and its release. */
    s_power.pressed = gpio_get_level(POWER_GPIO) == 0;
    s_power_armed = !s_power.pressed;

    const gpio_config_t charge = {
        .pin_bit_mask = 1ULL << CHARGE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&charge), TAG, "charge status");

    const adc_oneshot_unit_init_cfg_t adc_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&adc_cfg, &s_adc), TAG, "adc");
    const adc_oneshot_chan_cfg_t ch_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, BATT_ADC, &ch_cfg), TAG, "adc channel");
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .chan = BATT_ADC,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration unavailable; battery voltage is uncalibrated");
    }
#endif
    return ESP_OK;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t bl_channel = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    if (ledc_timer_config(&bl_timer) != ESP_OK || ledc_channel_config(&bl_channel) != ESP_OK) {
        ESP_LOGE(TAG, "backlight PWM");
        return NULL;
    }

    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_RES * DRAW_BUF_LINES * 2,
    };
    if (spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus");
        return NULL;
    }
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 3,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &s_panel_io) != ESP_OK) {
        ESP_LOGE(TAG, "panel IO");
        return NULL;
    }
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(s_panel_io, &panel_cfg, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "ST7789");
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
        return NULL;
    }
    const esp_lv_adapter_display_config_t display_cfg = {
        .panel = s_panel,
        .panel_io = s_panel_io,
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
    lv_display_t *display = esp_lv_adapter_register_display(&display_cfg);
    if (!display) {
        return NULL;
    }

    esp_lcd_panel_io_handle_t touch_io;
    const esp_lcd_panel_io_i2c_config_t touch_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    if (esp_lcd_new_panel_io_i2c(s_i2c, &touch_io_cfg, &touch_io) != ESP_OK) {
        ESP_LOGE(TAG, "touch IO");
        return NULL;
    }
    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_RES,
        .y_max = LCD_RES,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    if (esp_lcd_touch_new_i2c_cst816s(touch_io, &touch_cfg, &s_touch) != ESP_OK) {
        ESP_LOGE(TAG, "CST816S");
        return NULL;
    }
    const esp_lv_adapter_touch_config_t lv_touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, s_touch);
    *touch = esp_lv_adapter_register_touch(&lv_touch_cfg);
    if (!*touch || esp_lv_adapter_start() != ESP_OK) {
        return NULL;
    }
    return display;
}

static bool display_lock(int timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
}

static void set_brightness(int pct)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void panel_sleep(bool sleep)
{
    esp_lcd_panel_io_tx_param(s_panel_io, sleep ? 0x10 : 0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void display_pause(bool pause)
{
    if (pause) {
        esp_lv_adapter_pause(-1);
    } else {
        esp_lv_adapter_resume();
    }
}

static esp_err_t audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone)
{
    i2s_chan_handle_t tx, rx;
    i2s_chan_config_t channel_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_cfg.auto_clear = true;
    /* Keep 40 ms of DMA in each direction. The IDF defaults reserve 90 ms
     * per channel, which leaves too little contiguous DMA RAM in the
     * realtime-voice build for the second half of this duplex bus. */
    channel_cfg.dma_desc_num = I2S_DMA_DESC;
    channel_cfg.dma_frame_num = I2S_DMA_FRAMES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_cfg, &tx, &rx), TAG, "I2S channel");
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "I2S RX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "I2S TX enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "I2S RX enable");

    const audio_codec_data_if_t *speaker_data = audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){
        .port = I2S_NUM_0, .rx_handle = NULL, .tx_handle = tx,
    });
    const audio_codec_ctrl_if_t *speaker_ctrl = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
        .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c,
    });
    const audio_codec_gpio_if_t *speaker_gpio = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(speaker_data && speaker_ctrl && speaker_gpio, ESP_ERR_NO_MEM, TAG, "speaker interfaces");
    const audio_codec_if_t *speaker_codec = es8311_codec_new(&(es8311_codec_cfg_t){
        .ctrl_if = speaker_ctrl,
        .gpio_if = speaker_gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PA_EN,
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    });
    ESP_RETURN_ON_FALSE(speaker_codec, ESP_FAIL, TAG, "ES8311");
    *speaker = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = speaker_codec, .data_if = speaker_data,
    });
    ESP_RETURN_ON_FALSE(*speaker, ESP_FAIL, TAG, "speaker device");

    const audio_codec_data_if_t *mic_data = audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){
        .port = I2S_NUM_0, .rx_handle = rx, .tx_handle = NULL,
    });
    const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
        .port = I2C_NUM_0, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = s_i2c,
    });
    ESP_RETURN_ON_FALSE(mic_data && mic_ctrl, ESP_ERR_NO_MEM, TAG, "microphone interfaces");
    const audio_codec_if_t *mic_codec = es7210_codec_new(&(es7210_codec_cfg_t){
        .ctrl_if = mic_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    });
    ESP_RETURN_ON_FALSE(mic_codec, ESP_FAIL, TAG, "ES7210");
    *microphone = esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = mic_codec, .data_if = mic_data,
    });
    ESP_RETURN_ON_FALSE(*microphone, ESP_FAIL, TAG, "microphone device");

    ESP_LOGI(TAG, "audio: ES8311 speaker and ES7210 microphones on I2S0");
    return ESP_OK;
}

static unsigned poll_power(void)
{
    unsigned edge = muse_gpio_button_poll(&s_power);
    unsigned out = 0;
    int64_t now = esp_timer_get_time();
    if (edge & MUSE_BTN_TALK_PRESS) {
        if (s_power_armed) {
            s_power_held_at = now;
            s_power_long = false;
            out |= MUSE_BTN_POWER_PRESS;
        }
    }
    if (edge & MUSE_BTN_TALK_RELEASE) {
        if (s_power_armed) {
            out |= MUSE_BTN_POWER_RELEASE;
        } else {
            s_power_armed = true;
        }
        s_power_held_at = 0;
        s_power_long = false;
    }
    if (s_power_armed && s_power.pressed && !s_power_long && s_power_held_at &&
        now - s_power_held_at >= POWER_HOLD_US) {
        s_power_long = true;
        out |= MUSE_BTN_POWER_LONG;
    }
    return out;
}

static unsigned poll_buttons(void)
{
    return muse_gpio_button_poll(&s_talk) |
           (muse_gpio_button_poll(&s_aux) << 2) |
           poll_power();
}

static void wait_buttons(int timeout_ms)
{
    muse_gpio_buttons_wait((muse_gpio_button_t *const[]){ &s_talk, &s_aux, &s_power }, 3, timeout_ms);
}

static esp_err_t read_power(muse_power_t *out)
{
    int sum = 0;
    for (int i = 0; i < 8; ++i) {
        int sample;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, BATT_ADC, &sample), TAG, "ADC read");
        sum += sample;
    }
    int raw = sum / 8;
    int mv;
    if (s_cali) {
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw, &mv), TAG, "ADC calibration");
    } else {
        mv = raw * 3300 / 4096;
    }
    out->battery_mv = mv * 3;
    int pct = (out->battery_mv - 3300) * 100 / (4200 - 3300);
    out->battery_pct = out->battery_mv < 2500 ? -1 : pct < 0 ? 0 : pct > 100 ? 100 : pct;
    out->charging = gpio_get_level(CHARGE_GPIO) == 0;
    out->usb = usb_serial_jtag_is_connected();
    return ESP_OK;
}

static esp_err_t power_off(void)
{
    set_brightness(0);
    esp_lcd_panel_disp_on_off(s_panel, false);
    gpio_set_level(POWER_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    return ESP_FAIL;
}

static const muse_board_t s_board = {
    .name = "Waveshare ESP32-S3-Touch-LCD-1.54",
    .width = LCD_RES,
    .height = LCD_RES,
    .round = false,
    .no_ring = true,
    .touch = true,
    .diagonal_in = 1.54f,
    .talk_button = "+/KEY",
    .aux_button = "BOOT/-",
    .power_button = "PWR",
    .talk_hint = { LV_ALIGN_TOP_LEFT, 8, 8 },
    .aux_hint = { LV_ALIGN_TOP_RIGHT, -8, 8 },
    .power_hint = { LV_ALIGN_TOP_MID, 0, 8 },
    .frame_ms = 40,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness,
    .panel_sleep = panel_sleep,
    .display_pause = display_pause,
    .audio_init = audio_init,
    .mic_slot = 0,
    .poll_buttons = poll_buttons,
    .wait_buttons = wait_buttons,
    .read_power = read_power,
    .power_off = power_off,
};

const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
