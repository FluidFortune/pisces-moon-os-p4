// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_bsp.c — ELECROW CrowPanel Advanced 7" BSP
//
//  Written against the published ESP-IDF reference drivers:
//    - esp_lcd MIPI-DSI bus + DPI panel
//    - esp_lcd_ili9881c managed component (panel driver)
//    - esp_lcd_touch_gt911 managed component (capacitive touch)
//    - esp_lvgl_port managed component (LVGL plumbing)
//    - SDMMC + ledc + i2c_master + i2s_std drivers from IDF
//
//  References:
//    - docs.espressif.com … esp32p4 lcd dsi_lcd guide
//    - components.espressif.com / espressif/esp_lcd_touch_gt911
//    - components.espressif.com / espressif/esp_lvgl_port
//
//  Eric should verify pins against the ELECROW schematic
//  before flashing hardware. All pin assignments are in
//  pm_bsp.h so they're reachable in one edit.
// ============================================================

#include "pm_bsp.h"
#include "pm_hal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"

// Managed components (idf_component.yml dependencies)
#include "esp_lcd_ili9881c.h"      // ILI9881C 1024x600 panel
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"

static const char* TAG = "PM_BSP";

// ── State ───────────────────────────────────────────────────
static esp_lcd_dsi_bus_handle_t s_dsi_bus    = NULL;
static esp_lcd_panel_io_handle_t s_dbi_io    = NULL;
static esp_lcd_panel_handle_t   s_dpi_panel  = NULL;
static esp_lcd_panel_handle_t   s_panel      = NULL;
static esp_lcd_touch_handle_t   s_touch      = NULL;
static i2c_master_bus_handle_t  s_i2c_bus    = NULL;

static lv_display_t* s_lvgl_disp = NULL;
static lv_indev_t*   s_lvgl_indev = NULL;

static bool s_lvgl_tick_running = false;

// ─────────────────────────────────────────────
//  Backlight (LEDC PWM + power rail enable)
//  ELECROW exposes two pins: BL_POWER (rail enable, GPIO) and
//  BL_EN (LEDC PWM). The power pin must be high before the
//  PWM line does anything useful.
// ─────────────────────────────────────────────
static esp_err_t _backlight_init(void) {
    // Power rail enable as plain GPIO output, default off
    gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << PM_PIN_LCD_BL_PWR,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pwr), TAG, "bl_pwr_gpio");
    gpio_set_level(PM_PIN_LCD_BL_PWR, 0);

    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = PM_LCD_BL_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "ledc_timer_config");

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = PM_LCD_BL_LEDC_CH,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = PM_PIN_LCD_BL,
        .duty       = 0,
        .hpoint     = 0,
    };
    return ledc_channel_config(&ch);
}

void pm_bsp_set_backlight(uint8_t pct) {
    if (pct > 100) pct = 100;
    // Power rail follows on/off; PWM controls brightness.
    gpio_set_level(PM_PIN_LCD_BL_PWR, pct > 0 ? 1 : 0);
    uint32_t duty = (uint32_t)pct * 1023 / 100;
    ledc_set_duty (LEDC_LOW_SPEED_MODE, PM_LCD_BL_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PM_LCD_BL_LEDC_CH);
}

// ─────────────────────────────────────────────
//  I2C bus for touch
// ─────────────────────────────────────────────
static esp_err_t _i2c_init(void) {
    i2c_master_bus_config_t cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .scl_io_num                   = PM_PIN_I2C_SCL,
        .sda_io_num                   = PM_PIN_I2C_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

// ─────────────────────────────────────────────
//  MIPI-DSI bus + panel (ILI9881C @ 1024×600)
// ─────────────────────────────────────────────
static esp_err_t _display_init(void) {
    ESP_LOGI(TAG, "MIPI-DSI bus + ILI9881C panel init");

    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = 2,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = PM_MIPI_LANE_BITRATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus),
                          TAG, "new_dsi_bus");

    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_dbi_io),
                          TAG, "new_panel_io_dbi");

    // DPI (pixel data plane). Timings here are the standard
    // 1024×600 60Hz numbers used by ESP-BSP reference boards.
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel    = 0,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 52,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = 1,
        .video_timing = {
            .h_size            = PM_LCD_H_RES,
            .v_size            = PM_LCD_V_RES,
            .hsync_pulse_width = 10,
            .hsync_back_porch  = 160,
            .hsync_front_porch = 160,
            .vsync_pulse_width = 1,
            .vsync_back_porch  = 23,
            .vsync_front_porch = 12,
        },
        .flags.use_dma2d    = true,
    };

    esp_lcd_panel_ili9881c_vendor_config_t vendor = {
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = PM_LCD_BIT_PER_PIXEL,
        .vendor_config  = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9881c(s_dbi_io, &panel_cfg, &s_panel),
                          TAG, "new_panel_ili9881c");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init (s_panel), TAG, "panel_init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "panel_on");

    return ESP_OK;
}

// ─────────────────────────────────────────────
//  GT911 capacitive touch
// ─────────────────────────────────────────────
static esp_err_t _touch_init(void) {
    ESP_LOGI(TAG, "GT911 touch init");

    esp_lcd_panel_io_handle_t tio = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &tio),
                          TAG, "touch_io_i2c");

    esp_lcd_touch_io_gt911_config_t gt_cfg = {
        .dev_addr = io_cfg.dev_addr,
    };
    esp_lcd_touch_config_t cfg = {
        .x_max         = PM_LCD_H_RES,
        .y_max         = PM_LCD_V_RES,
        .rst_gpio_num  = PM_PIN_TOUCH_RST,
        .int_gpio_num  = PM_PIN_TOUCH_INT,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &gt_cfg,
    };
    return esp_lcd_touch_new_i2c_gt911(tio, &cfg, &s_touch);
}

// ─────────────────────────────────────────────
//  LVGL display + indev registration
// ─────────────────────────────────────────────
static esp_err_t _lvgl_init(void) {
    ESP_LOGI(TAG, "LVGL port init");

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl_port_init");

    // Register the MIPI-DSI panel as an LVGL display.
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = s_dbi_io,
        .panel_handle = s_panel,
        .buffer_size  = PM_LCD_H_RES * 100,   // 100 lines × 1024 × 2 = 200KB
        .double_buffer = true,
        .hres         = PM_LCD_H_RES,
        .vres         = PM_LCD_V_RES,
        .monochrome   = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma   = false,    // we'll use PSRAM for large buffers
            .buff_spiram = true,
            .swap_bytes  = false,
        },
    };

    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .use_dma2d = true,
        },
    };

    s_lvgl_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!s_lvgl_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_dsi failed");
        return ESP_FAIL;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_lvgl_disp,
        .handle = s_touch,
    };
    s_lvgl_indev = lvgl_port_add_touch(&touch_cfg);
    if (!s_lvgl_indev) {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────
esp_err_t pm_bsp_init(void) {
    ESP_LOGI(TAG, "BSP init…");
    ESP_RETURN_ON_ERROR(_backlight_init(), TAG, "backlight");
    pm_bsp_set_backlight(0);    // dark until first frame ready

    ESP_RETURN_ON_ERROR(_i2c_init(),     TAG, "i2c");
    ESP_RETURN_ON_ERROR(_display_init(), TAG, "display");
    ESP_RETURN_ON_ERROR(_touch_init(),   TAG, "touch");
    ESP_RETURN_ON_ERROR(_lvgl_init(),    TAG, "lvgl");

    pm_bsp_set_backlight(80);    // up after init succeeds
    ESP_LOGI(TAG, "BSP ready: %dx%d MIPI-DSI + GT911", PM_LCD_H_RES, PM_LCD_V_RES);
    return ESP_OK;
}

// ─────────────────────────────────────────────
//  LVGL tick task (esp_lvgl_port handles its own
//  task internally, but we expose a public start
//  for symmetry; second call is a no-op).
// ─────────────────────────────────────────────
void pm_bsp_start_lvgl_tick_task(void) {
    if (s_lvgl_tick_running) return;
    s_lvgl_tick_running = true;
    // esp_lvgl_port already spawned its own task in lvgl_port_init.
    // Nothing to do here — kept for API stability.
}

lv_display_t* pm_bsp_lvgl_display(void) { return s_lvgl_disp; }
lv_indev_t*   pm_bsp_lvgl_touch(void)   { return s_lvgl_indev; }
