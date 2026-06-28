// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_bsp_hi8561.h — HI8561 portrait MIPI-DSI panel driver
//
//  The HI8561 is the bonded LCD controller used on the LilyGO
//  T-Display-P4 (540×1168 IPS, 2-lane MIPI-DSI, 16.7M colour).
//  Espressif's `esp_lcd_*` managed components don't ship a
//  driver for it, so this is a hand-port of LilyGO's reference
//  C++ driver (hi8561_driver.cpp in their public T-Display-P4
//  repo, Apache-2.0) to pure C. The vendor-specific init
//  command table is preserved byte-for-byte from that source.
//
//  Usage: create the panel via esp_lcd_new_panel_hi8561() with
//  a pm_hi8561_vendor_config_t in `vendor_config`. The standard
//  esp_lcd_panel_handle_t API (reset / init / disp_on_off /
//  mirror / disp_sleep) works as expected afterward.
// ============================================================

#ifndef PM_BSP_HI8561_H
#define PM_BSP_HI8561_H

#include <stdint.h>
#include <stddef.h>
#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

// One entry in the HI8561 init command table.
typedef struct {
    int          cmd;         // LCD command byte
    const void*  data;        // payload buffer
    size_t       data_bytes;  // payload length
    unsigned int delay_ms;    // delay after this command
} pm_hi8561_init_cmd_t;

// Vendor-config block passed via panel_dev_config->vendor_config.
// Pass init_cmds=NULL to use the default LilyGO sequence (which is
// what the T-Display-P4 ships with).
typedef struct {
    const pm_hi8561_init_cmd_t* init_cmds;
    uint16_t                    init_cmds_size;
    struct {
        esp_lcd_dsi_bus_handle_t          dsi_bus;
        const esp_lcd_dpi_panel_config_t* dpi_config;
        uint8_t                           lane_num;  // 0 → default 2
    } mipi_config;
} pm_hi8561_vendor_config_t;

// Create a new HI8561 panel. The returned handle goes through
// esp_lcd_panel_reset() then esp_lcd_panel_init() like any other
// MIPI-DPI panel.
esp_err_t pm_lcd_new_panel_hi8561(esp_lcd_panel_io_handle_t io,
                                  const esp_lcd_panel_dev_config_t* panel_dev_config,
                                  esp_lcd_panel_handle_t* ret_panel);

#ifdef __cplusplus
}
#endif

#endif  // SOC_MIPI_DSI_SUPPORTED
#endif  // PM_BSP_HI8561_H
