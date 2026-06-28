// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
//
// HI8561 vendor-specific init command table and panel-driver
// scaffolding ported from LilyGO's hi8561_driver.cpp
// (Apache-2.0, SPDX-FileCopyrightText: 2023-2024 Espressif).
//
// Pisces Moon-side wrapping is C; the init byte sequence is
// preserved verbatim from the LilyGO reference. The HI8561 is
// the bonded controller on the LilyGO T-Display-P4 540×1168
// portrait IPS panel.

#include "pm_bsp_hi8561.h"

#if SOC_MIPI_DSI_SUPPORTED

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_mipi_dsi.h"

#include <stdlib.h>

static const char* TAG = "PM_HI8561";

#define HI8561_CMD_SHLR_BIT         (1ULL << 0)
#define HI8561_CMD_UPDN_BIT         (1ULL << 1)
#define HI8561_MDCTL_VALUE_DEFAULT  (0x01)

// ── Vendor init sequence ─────────────────────────────────────
// Preserved exactly as published by LilyGO. The 2-lane MIPI-DSI
// configuration is selected via the 0xCC register (value 0x31);
// 1-lane (0x30) and 4-lane (0x33) variants are commented out for
// reference. Display ON (0x29) and Sleep OFF (0x11) terminate.
static const pm_hi8561_init_cmd_t HI8561_INIT_DEFAULT[] = {
    /**** CMD_Page 3 ****/
    {0xDF, (uint8_t[]){0x90, 0x69, 0xF9}, 3, 0},
    {0xDE, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x0F, 0x10, 0x43, 0x50, 0x32, 0x44, 0x44}, 7, 0},
    {0xBF, (uint8_t[]){0x46, 0x32}, 2, 0},
    {0xC0, (uint8_t[]){0x01, 0xAD, 0x01, 0xAD}, 4, 0},
    {0xBD, (uint8_t[]){0x00, 0xB4}, 2, 0},
    {0xC6, (uint8_t[]){0x00, 0x7D, 0x00, 0xC8, 0x00, 0x17, 0x1A, 0x82, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01}, 23, 0},
    {0xC8, (uint8_t[]){0x23, 0x48, 0x87}, 3, 0},
    {0xCC, (uint8_t[]){0x31}, 1, 0},   // 2-lane MIPI-DSI
    {0xBC, (uint8_t[]){0x2E, 0x80, 0x84}, 3, 0},
    {0xC3, (uint8_t[]){0x3B, 0x01, 0x02, 0x05, 0x0C, 0x0C, 0x75, 0x0A, 0x79, 0x0A, 0x79, 0x02, 0x6E, 0x02, 0x6E, 0x02, 0x6E, 0x0A, 0x0D, 0x0A, 0x0F, 0x0A, 0x0F, 0x0A, 0x0F}, 25, 0},
    {0xC4, (uint8_t[]){0x01, 0x02, 0x05, 0x0C, 0x0C, 0x75, 0x0A, 0x79, 0x0A, 0x79, 0x02, 0x6E, 0x02, 0x6E, 0x02, 0x6E, 0x0A, 0x0D, 0x0A, 0x0F, 0x0A, 0x0F, 0x0A, 0x0F}, 24, 0},
    {0xC5, (uint8_t[]){0x03, 0x05, 0x0C, 0x0C, 0x75, 0x0A, 0x79, 0x0A, 0x79, 0x02, 0x6E, 0x02, 0x6E, 0x02, 0x6E, 0x0A, 0x0D, 0x0A, 0x0F, 0x0A, 0x0F, 0x0A, 0x0F}, 23, 0},
    {0xD7, (uint8_t[]){0x00, 0x0A, 0x63, 0x0A, 0x63, 0x0A, 0x63, 0x0A, 0x63, 0x0A, 0x63, 0x0A, 0x63, 0x0A, 0x63, 0x0A, 0x63}, 17, 0},
    {0xCB, (uint8_t[]){0x7F, 0x78, 0x71, 0x64, 0x5A, 0x58, 0x4B, 0x51, 0x3A, 0x53, 0x51, 0x4F, 0x6A, 0x54, 0x57, 0x46, 0x3F, 0x2F, 0x1B, 0x0F, 0x08, 0x7F, 0x78, 0x71, 0x64, 0x5A, 0x58, 0x4B, 0x51, 0x3A, 0x53, 0x51, 0x4F, 0x6A, 0x54, 0x57, 0x46, 0x3F, 0x2F, 0x1B, 0x0F, 0x08, 0x00}, 43, 0},
    {0xCE, (uint8_t[]){0x00, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C}, 23, 0},
    {0xCF, (uint8_t[]){0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 45, 0},
    {0xD0, (uint8_t[]){0x00, 0x1F, 0x1F, 0x11, 0x1E, 0x1F, 0x0F, 0x0F, 0x0D, 0x0D, 0x0B, 0x0B, 0x09, 0x09, 0x07, 0x07, 0x05, 0x05, 0x01, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 29, 0},
    {0xD1, (uint8_t[]){0x00, 0x1F, 0x1F, 0x10, 0x1E, 0x1F, 0x0E, 0x0E, 0x0C, 0x0C, 0x0A, 0x0A, 0x08, 0x08, 0x06, 0x06, 0x04, 0x04, 0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 29, 0},
    {0xD2, (uint8_t[]){0x00, 0x5F, 0x1F, 0x10, 0x1F, 0x1E, 0x08, 0x08, 0x4A, 0x0A, 0x0C, 0x0C, 0x0E, 0x0E, 0x04, 0x04, 0x06, 0x06, 0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 29, 0},
    {0xD3, (uint8_t[]){0x00, 0x1F, 0x1F, 0x11, 0x1F, 0x1E, 0x09, 0x09, 0x0B, 0x0B, 0x0D, 0x0D, 0x0F, 0x0F, 0x05, 0x05, 0x07, 0x07, 0x01, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 29, 0},
    {0xD4, (uint8_t[]){0x00, 0x20, 0x0B, 0x00, 0x0D, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x00, 0x81, 0x04, 0xAE, 0x04, 0xB0, 0x04, 0xB2, 0x04, 0xB4, 0x04, 0xB6, 0x04, 0xB8, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x06, 0x44, 0x06, 0x46, 0x03, 0x03, 0x00, 0x00, 0x07, 0x00, 0x06, 0x04, 0xA7, 0x04, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00, 0x20, 0x00}, 87, 0},
    {0xD5, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x07, 0x32, 0x5A, 0x00, 0x00, 0x3C, 0x00, 0x1E, 0x00, 0x1E, 0xB3, 0x00, 0x0F, 0x06, 0x0C, 0x00, 0x71, 0x20, 0x04, 0x10, 0x04, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x00, 0x00, 0x00, 0x1F, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 61, 0},
    {0xCD, (uint8_t[]){0x00, 0x00}, 2, 0},

    {0xDE, (uint8_t[]){0x01}, 1, 0},
    {0xB9, (uint8_t[]){0x00, 0xFF, 0xFF, 0x04}, 4, 0},
    {0xC7, (uint8_t[]){0x1F, 0x14, 0x0E}, 3, 0},

    {0xDE, (uint8_t[]){0x02}, 1, 0},
    {0xE5, (uint8_t[]){0x00, 0x60, 0x60, 0x02, 0x18, 0x60, 0x18, 0x60, 0x09, 0x04, 0x00, 0xC5, 0x01, 0x2C, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04}, 24, 0},
    {0xE6, (uint8_t[]){0x10, 0x10, 0x82}, 3, 0},
    {0xC4, (uint8_t[]){0x00, 0x11, 0x07, 0x00, 0x11, 0x01, 0x08}, 7, 0},
    {0xC3, (uint8_t[]){0x20, 0xFF}, 2, 0},
    {0xBD, (uint8_t[]){0x1B}, 1, 0},
    {0xC6, (uint8_t[]){0x4A, 0x00}, 2, 0},
    {0xCD, (uint8_t[]){0x14, 0x64, 0x11, 0x40}, 4, 0},
    {0xC1, (uint8_t[]){0x00, 0x40, 0x00, 0x02, 0x02, 0x02, 0x02, 0x7F, 0x00, 0x00}, 10, 0},
    {0xB3, (uint8_t[]){0x00, 0xA8}, 2, 0},
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0x40, 0x43, 0x04}, 11, 0},
    {0xC2, (uint8_t[]){0x02, 0x42, 0x50, 0x00, 0x02, 0xE4, 0x61, 0x73, 0xF9, 0x08}, 10, 0},
    {0xEC, (uint8_t[]){0x07, 0x07, 0x40, 0x00, 0x22, 0x02, 0x00, 0xFF, 0x08, 0x7C, 0x00, 0x00, 0x00, 0x00}, 14, 0},

    {0xDE, (uint8_t[]){0x03}, 1, 0},
    {0xD1, (uint8_t[]){0x00, 0x00, 0x21, 0xFF, 0x00}, 5, 0},

    {0xDE, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 0, 30},
    {0x11, (uint8_t[]){0x00}, 0, 120},   // Sleep Out
    {0x29, (uint8_t[]){0x00}, 0, 50},    // Display ON
};

// ── Internal panel struct ────────────────────────────────────
typedef struct {
    esp_lcd_panel_io_handle_t io;
    int                       reset_gpio_num;
    uint8_t                   madctl_val;
    const pm_hi8561_init_cmd_t* init_cmds;
    uint16_t                  init_cmds_size;
    uint8_t                   lane_num;
    bool                      reset_active_high;
    esp_err_t (*dpi_del)(esp_lcd_panel_t*);
    esp_err_t (*dpi_init)(esp_lcd_panel_t*);
} pm_hi8561_t;

// Forward decls for the function-pointer overrides we install on
// the underlying esp_lcd MIPI-DPI panel.
static esp_err_t pm_hi8561_del(esp_lcd_panel_t* panel);
static esp_err_t pm_hi8561_init(esp_lcd_panel_t* panel);
static esp_err_t pm_hi8561_reset(esp_lcd_panel_t* panel);
static esp_err_t pm_hi8561_mirror(esp_lcd_panel_t* panel, bool mirror_x, bool mirror_y);
static esp_err_t pm_hi8561_invert_color(esp_lcd_panel_t* panel, bool invert);
static esp_err_t pm_hi8561_sleep(esp_lcd_panel_t* panel, bool sleep);
static esp_err_t pm_hi8561_on_off(esp_lcd_panel_t* panel, bool on);

// ── Public constructor ───────────────────────────────────────
esp_err_t pm_lcd_new_panel_hi8561(esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t* panel_dev_config,
                                   esp_lcd_panel_handle_t* ret_panel) {
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel,
                        ESP_ERR_INVALID_ARG, TAG, "bad args");
    pm_hi8561_vendor_config_t* vc =
        (pm_hi8561_vendor_config_t*)panel_dev_config->vendor_config;
    ESP_RETURN_ON_FALSE(vc && vc->mipi_config.dpi_config && vc->mipi_config.dsi_bus,
                        ESP_ERR_INVALID_ARG, TAG, "bad vendor config");

    pm_hi8561_t* hi = (pm_hi8561_t*)calloc(1, sizeof(*hi));
    ESP_RETURN_ON_FALSE(hi, ESP_ERR_NO_MEM, TAG, "no mem for panel");

    esp_err_t ret = ESP_OK;

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
            .mode         = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "rst gpio");
    }

    hi->io                = io;
    hi->init_cmds         = vc->init_cmds ? vc->init_cmds : HI8561_INIT_DEFAULT;
    hi->init_cmds_size    = vc->init_cmds
                            ? vc->init_cmds_size
                            : (uint16_t)(sizeof(HI8561_INIT_DEFAULT) /
                                          sizeof(HI8561_INIT_DEFAULT[0]));
    hi->lane_num          = vc->mipi_config.lane_num ? vc->mipi_config.lane_num : 2;
    hi->reset_gpio_num    = panel_dev_config->reset_gpio_num;
    hi->reset_active_high = panel_dev_config->flags.reset_active_high;
    hi->madctl_val        = HI8561_MDCTL_VALUE_DEFAULT;

    // Build the underlying MIPI-DPI panel, then steal its function
    // pointers so we can layer the HI8561-specific init/reset/etc.
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vc->mipi_config.dsi_bus,
                                             vc->mipi_config.dpi_config,
                                             ret_panel),
                     err, TAG, "create MIPI DPI panel failed");

    hi->dpi_del  = (*ret_panel)->del;
    hi->dpi_init = (*ret_panel)->init;

    (*ret_panel)->del          = pm_hi8561_del;
    (*ret_panel)->init         = pm_hi8561_init;
    (*ret_panel)->reset        = pm_hi8561_reset;
    (*ret_panel)->mirror       = pm_hi8561_mirror;
    (*ret_panel)->invert_color = pm_hi8561_invert_color;
    (*ret_panel)->disp_sleep   = pm_hi8561_sleep;
    (*ret_panel)->disp_on_off  = pm_hi8561_on_off;
    (*ret_panel)->user_data    = hi;
    return ESP_OK;

err:
    if (hi) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(hi);
    }
    return ret;
}

// ── Helpers ──────────────────────────────────────────────────
static esp_err_t pm_hi8561_send_init_cmds(pm_hi8561_t* hi) {
    for (int i = 0; i < hi->init_cmds_size; i++) {
        esp_lcd_panel_io_tx_param(hi->io, hi->init_cmds[i].cmd,
                                   hi->init_cmds[i].data,
                                   hi->init_cmds[i].data_bytes);
        if (hi->init_cmds[i].delay_ms)
            vTaskDelay(pdMS_TO_TICKS(hi->init_cmds[i].delay_ms));
    }
    return ESP_OK;
}

static esp_err_t pm_hi8561_del(esp_lcd_panel_t* panel) {
    pm_hi8561_t* hi = (pm_hi8561_t*)panel->user_data;
    if (hi->reset_gpio_num >= 0) gpio_reset_pin(hi->reset_gpio_num);
    hi->dpi_del(panel);
    free(hi);
    return ESP_OK;
}

static esp_err_t pm_hi8561_init(esp_lcd_panel_t* panel) {
    pm_hi8561_t* hi = (pm_hi8561_t*)panel->user_data;
    ESP_RETURN_ON_ERROR(pm_hi8561_send_init_cmds(hi), TAG, "init seq");
    return hi->dpi_init(panel);
}

static esp_err_t pm_hi8561_reset(esp_lcd_panel_t* panel) {
    pm_hi8561_t* hi = (pm_hi8561_t*)panel->user_data;
    if (hi->reset_gpio_num >= 0) {
        gpio_set_level(hi->reset_gpio_num, hi->reset_active_high ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(hi->reset_gpio_num, hi->reset_active_high ? 0 : 1);
        vTaskDelay(pdMS_TO_TICKS(20));
    } else if (hi->io) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(hi->io, LCD_CMD_SWRESET,
                                                       NULL, 0),
                            TAG, "swreset");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

static esp_err_t pm_hi8561_sleep(esp_lcd_panel_t* panel, bool sleep) {
    pm_hi8561_t* hi = (pm_hi8561_t*)panel->user_data;
    uint8_t cmd = sleep ? 0x10 : 0x11;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(hi->io, cmd, NULL, 0),
                        TAG, "sleep cmd");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t pm_hi8561_on_off(esp_lcd_panel_t* panel, bool on) {
    pm_hi8561_t* hi = (pm_hi8561_t*)panel->user_data;
    uint8_t cmd = on ? 0x29 : 0x28;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(hi->io, cmd, NULL, 0),
                        TAG, "on/off cmd");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t pm_hi8561_mirror(esp_lcd_panel_t* panel,
                                    bool mirror_x, bool mirror_y) {
    pm_hi8561_t* hi = (pm_hi8561_t*)panel->user_data;
    ESP_RETURN_ON_FALSE(hi->io, ESP_ERR_INVALID_STATE, TAG, "no io");
    uint8_t v = hi->madctl_val;
    v = mirror_x ? (v |  HI8561_CMD_SHLR_BIT) : (v & ~HI8561_CMD_SHLR_BIT);
    v = mirror_y ? (v |  HI8561_CMD_UPDN_BIT) : (v & ~HI8561_CMD_UPDN_BIT);
    uint8_t buf = v;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(hi->io, LCD_CMD_MADCTL,
                                                   &buf, 1),
                        TAG, "madctl");
    hi->madctl_val = v;
    return ESP_OK;
}

static esp_err_t pm_hi8561_invert_color(esp_lcd_panel_t* panel, bool invert) {
    pm_hi8561_t* hi = (pm_hi8561_t*)panel->user_data;
    ESP_RETURN_ON_FALSE(hi->io, ESP_ERR_INVALID_STATE, TAG, "no io");
    uint8_t cmd = invert ? LCD_CMD_INVON : LCD_CMD_INVOFF;
    return esp_lcd_panel_io_tx_param(hi->io, cmd, NULL, 0);
}

#endif  // SOC_MIPI_DSI_SUPPORTED
