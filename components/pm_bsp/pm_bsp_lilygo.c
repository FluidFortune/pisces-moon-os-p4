// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_bsp_lilygo.c — LilyGO T-Display-P4 BSP
//
//  Brings up:
//    - Two I2C buses (I2C1 on GPIO 7/8, I2C2 on GPIO 20/21)
//    - MIPI-DSI display (HI8561 540x1168 portrait, RGB565)
//    - HI8561 integrated touch over I2C1 (addr 0x68)
//    - SD card via SDMMC 4-bit (FAT, /sd)
//    - Backlight PWM (single rail; rail power gated via XL9535)
//    - LVGL display + indev driver registration
//
//  IMPORTANT — boot order:
//    XL9535 power-management lives in a separate component
//    (pm_xl9535) to avoid a CMake dependency cycle. main.c is
//    responsible for orchestrating:
//
//      pm_bsp_init_buses();   // I2C up
//      pm_xl9535_init();
//      pm_xl9535_boot_sequence();
//      pm_bsp_init();         // display, touch, SD, LVGL
//
//  This file is compiled only when PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4
//  is defined; the Elecrow boards keep building from pm_bsp.c
//  unchanged.
// ============================================================

#include "pm_bsp.h"
#include "pm_board.h"

#if defined(PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4)

#include "pm_bsp_hi8561.h"
#include "pm_hal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"

#include "lvgl.h"

#include <string.h>

static const char* TAG = "PM_BSP_LILY";

// ── State ────────────────────────────────────────────────────
static esp_lcd_dsi_bus_handle_t  s_dsi_bus    = NULL;
static esp_lcd_panel_io_handle_t s_dbi_io     = NULL;
static esp_lcd_panel_handle_t    s_panel      = NULL;
static esp_lcd_touch_handle_t    s_touch      = NULL;
static i2c_master_bus_handle_t   s_i2c1_bus   = NULL;
static i2c_master_bus_handle_t   s_i2c2_bus   = NULL;

static lv_display_t* s_lvgl_disp  = NULL;
static lv_indev_t*   s_lvgl_indev = NULL;

static bool          s_lvgl_tick_running = false;
static sdmmc_card_t* s_sd_card           = NULL;
static bool          s_buses_up          = false;

// ── I2C bus init (TWO buses on this board) ───────────────────
esp_err_t pm_bsp_init_buses(void) {
    if (s_buses_up) return ESP_OK;

    i2c_master_bus_config_t b1 = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .scl_io_num                   = PM_PIN_I2C_SCL,
        .sda_io_num                   = PM_PIN_I2C_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&b1, &s_i2c1_bus), TAG, "I2C1");
    ESP_LOGI(TAG, "I2C1 up SDA=IO%d SCL=IO%d", PM_PIN_I2C_SDA, PM_PIN_I2C_SCL);

    i2c_master_bus_config_t b2 = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_1,
        .scl_io_num                   = PM_PIN_I2C2_SCL,
        .sda_io_num                   = PM_PIN_I2C2_SDA,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&b2, &s_i2c2_bus), TAG, "I2C2");
    ESP_LOGI(TAG, "I2C2 up SDA=IO%d SCL=IO%d",
             PM_PIN_I2C2_SDA, PM_PIN_I2C2_SCL);

    s_buses_up = true;
    return ESP_OK;
}

// ── Public I2C bus helpers ───────────────────────────────────
static esp_err_t _bus_txrx(i2c_master_bus_handle_t bus, uint8_t addr,
                            const uint8_t* tx, size_t tx_len,
                            uint8_t* rx, size_t rx_len,
                            int timeout_ms) {
    if (!bus) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = PM_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (err != ESP_OK) return err;
    if (rx && rx_len) {
        err = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, timeout_ms);
    } else {
        err = i2c_master_transmit(dev, tx, tx_len, timeout_ms);
    }
    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t pm_bsp_i2c_transmit(uint8_t addr, const uint8_t* tx, size_t tx_len,
                              int timeout_ms) {
    return _bus_txrx(s_i2c1_bus, addr, tx, tx_len, NULL, 0, timeout_ms);
}

esp_err_t pm_bsp_i2c_transmit_receive(uint8_t addr,
                                      const uint8_t* tx, size_t tx_len,
                                      uint8_t* rx, size_t rx_len,
                                      int timeout_ms) {
    return _bus_txrx(s_i2c1_bus, addr, tx, tx_len, rx, rx_len, timeout_ms);
}

esp_err_t pm_bsp_i2c2_transmit(uint8_t addr,
                               const uint8_t* tx, size_t tx_len,
                               int timeout_ms) {
    return _bus_txrx(s_i2c2_bus, addr, tx, tx_len, NULL, 0, timeout_ms);
}

esp_err_t pm_bsp_i2c2_transmit_receive(uint8_t addr,
                                       const uint8_t* tx, size_t tx_len,
                                       uint8_t* rx, size_t rx_len,
                                       int timeout_ms) {
    return _bus_txrx(s_i2c2_bus, addr, tx, tx_len, rx, rx_len, timeout_ms);
}

// ── Backlight (single PWM pin; rail power gated via XL9535) ──
static esp_err_t _backlight_init(void) {
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = PM_LCD_BL_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "ledc_timer");

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
    uint32_t duty = (uint32_t)pct * 1023 / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PM_LCD_BL_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PM_LCD_BL_LEDC_CH);
}

// ── MIPI-DSI bus + HI8561 panel ──────────────────────────────
static esp_err_t _display_init(void) {
    ESP_LOGI(TAG, "MIPI-DSI + HI8561 panel init (%s)", PM_BOARD_PANEL_DETAIL);

    static esp_ldo_channel_handle_t s_dsi_phy_ldo = NULL;
    if (s_dsi_phy_ldo == NULL) {
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id    = 3,
            .voltage_mv = 2500,
        };
        ESP_RETURN_ON_ERROR(
            esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_phy_ldo),
            TAG, "DSI PHY LDO");
        ESP_LOGI(TAG, "MIPI-DSI PHY rail powered (LDO_VO3 @ 2500 mV)");
    }

    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = PM_BOARD_MIPI_DATA_LANE_NUM,
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

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel    = 0,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PM_BOARD_DPI_CLK_MHZ,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs            = 1,
        .video_timing = {
            .h_size            = PM_LCD_H_RES,
            .v_size            = PM_LCD_V_RES,
            .hsync_pulse_width = PM_BOARD_HSYNC_PW,
            .hsync_back_porch  = PM_BOARD_HSYNC_BP,
            .hsync_front_porch = PM_BOARD_HSYNC_FP,
            .vsync_pulse_width = PM_BOARD_VSYNC_PW,
            .vsync_back_porch  = PM_BOARD_VSYNC_BP,
            .vsync_front_porch = PM_BOARD_VSYNC_FP,
        },
        .flags.use_dma2d    = true,
    };

    pm_hi8561_vendor_config_t vendor = {
        .init_cmds      = NULL,  // use the default LilyGO sequence
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus    = s_dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num   = PM_BOARD_MIPI_DATA_LANE_NUM,
        },
    };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = PM_LCD_BIT_PER_PIXEL,
        .vendor_config  = &vendor,
    };
    ESP_RETURN_ON_ERROR(pm_lcd_new_panel_hi8561(s_dbi_io, &panel_cfg, &s_panel),
                          TAG, "new HI8561 panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true),
                          TAG, "panel on");
    ESP_LOGI(TAG, "HI8561 display on");
    return ESP_OK;
}

// ── Touch (HI8561 integrated, I2C1 addr 0x68) ────────────────
#define HI8561_TOUCH_REPORT_REG  0x01

static esp_err_t _touch_read_xy(uint16_t* x, uint16_t* y, bool* pressed) {
    if (!x || !y || !pressed) return ESP_ERR_INVALID_ARG;
    *pressed = false;
    uint8_t reg = HI8561_TOUCH_REPORT_REG;
    uint8_t buf[7] = {0};
    esp_err_t err = pm_bsp_i2c_transmit_receive(PM_HI8561_TOUCH_ADDR,
                                                  &reg, 1, buf, sizeof(buf), 30);
    if (err != ESP_OK) return err;
    uint8_t fingers = buf[0] & 0x0F;
    if (fingers == 0) return ESP_OK;
    uint16_t rx = (uint16_t)(((buf[1] & 0x0F) << 8) | buf[2]);
    uint16_t ry = (uint16_t)(((buf[3] & 0x0F) << 8) | buf[4]);
    if (rx >= PM_LCD_H_RES) rx = PM_LCD_H_RES - 1;
    if (ry >= PM_LCD_V_RES) ry = PM_LCD_V_RES - 1;
    *x = rx; *y = ry; *pressed = true;
    return ESP_OK;
}

static void _lvgl_touch_read(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    uint16_t x = 0, y = 0;
    bool pressed = false;
    if (_touch_read_xy(&x, &y, &pressed) == ESP_OK && pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
}

// ── SD card on SDMMC, 4-bit ──────────────────────────────────
static esp_err_t _sd_init(void) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk   = PM_PIN_SD_CLK;
    slot.cmd   = PM_PIN_SD_CMD;
    slot.d0    = PM_PIN_SD_D0;
    slot.d1    = PM_PIN_SD_D1;
    slot.d2    = PM_PIN_SD_D2;
    slot.d3    = PM_PIN_SD_D3;
    slot.width = 4;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sd", &host, &slot, &mount,
                                              &s_sd_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s) — continuing without SD",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SD mounted at /sd (%lluMB)",
             ((uint64_t)s_sd_card->csd.capacity) * s_sd_card->csd.sector_size /
                 (1024ULL * 1024ULL));
    return ESP_OK;
}

// ── LVGL plumbing ────────────────────────────────────────────
static esp_err_t _lvgl_init(void) {
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority     = 4,
        .task_stack        = 6144,
        .task_affinity     = 1,
        .task_max_sleep_ms = 500,
        .timer_period_ms   = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl_port_init");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = s_dbi_io,
        .panel_handle = s_panel,
        .buffer_size  = PM_LCD_H_RES * 80,
        .double_buffer= true,
        .hres         = PM_LCD_H_RES,
        .vres         = PM_LCD_V_RES,
        .monochrome   = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation     = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags        = { .buff_dma = true, .buff_spiram = false, .swap_bytes = false },
    };
    // avoid_tearing=1 uses the MIPI-DSI internal framebuffer directly
    // as LVGL's draw buffer. Eliminates tearing on the AMOLED panel
    // at the cost of a small frame-rate hit. Matches the LilyGO
    // T-Display-P4 reference configuration.
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = { .avoid_tearing = 1 },
    };
    s_lvgl_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    ESP_RETURN_ON_FALSE(s_lvgl_disp, ESP_FAIL, TAG, "add_disp_dsi");

    s_lvgl_indev = lv_indev_create();
    lv_indev_set_type(s_lvgl_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_lvgl_indev, _lvgl_touch_read);
    lv_indev_set_display(s_lvgl_indev, s_lvgl_disp);
    return ESP_OK;
}

// ── Top-level init ───────────────────────────────────────────
//
// Assumes pm_bsp_init_buses() and the XL9535 boot sequence have
// already run; main.c orchestrates the order.
esp_err_t pm_bsp_init(void) {
    ESP_LOGI(TAG, "==== %s ====", PM_BOARD_NAME);

    // Idempotent — covers the path where a caller skipped the
    // explicit init_buses call.
    ESP_RETURN_ON_ERROR(pm_bsp_init_buses(), TAG, "buses (late)");

    ESP_RETURN_ON_ERROR(_backlight_init(), TAG, "backlight");
    ESP_RETURN_ON_ERROR(_display_init(),   TAG, "display");
    pm_bsp_set_backlight(60);

    _sd_init();

    ESP_RETURN_ON_ERROR(_lvgl_init(), TAG, "lvgl");

    ESP_LOGI(TAG, "BSP init complete");
    return ESP_OK;
}

void pm_bsp_start_lvgl_tick_task(void) {
    s_lvgl_tick_running = true;
}

lv_display_t* pm_bsp_lvgl_display(void) { return s_lvgl_disp; }
lv_indev_t*   pm_bsp_lvgl_touch(void)   { return s_lvgl_indev; }

#endif  // PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4
