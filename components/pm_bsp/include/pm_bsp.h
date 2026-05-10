// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_bsp.h — Board Support Package for ELECROW CrowPanel
//                Advanced 7" ESP32-P4 HMI
//
//  Brings up:
//    - MIPI-DSI display (ILI9881C 1024×600 @60Hz, RGB565)
//    - GT911 multi-touch over I2C
//    - NS4168 audio codec (handed to pm_audio)
//    - SD card mount (FAT, /sd) — handed to pm_hal file ops
//    - Backlight PWM
//    - LVGL display + indev driver registration
//
//  Pin numbers below are the *published reference* for an
//  ESP32-P4 + 1024×600 MIPI-DSI panel + GT911. Eric should
//  cross-check against the ELECROW schematic before flashing
//  hardware. They're isolated to this file precisely so a
//  pin change doesn't ripple through the codebase.
//
//  Architecture:
//    pm_bsp_init()  — call once early in app_main, after HAL,
//                     before launcher and apps.
//    pm_bsp_start_lvgl_tick_task() — kicks the LVGL tick at
//                     5ms cadence on Core 1.
//    pm_bsp_set_backlight(uint8_t pct) — 0..100.
//
//  After pm_bsp_init() returns OK, lv_disp_t and lv_indev_t
//  are registered and apps can build screens.
// ============================================================

#ifndef PM_BSP_H
#define PM_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Display geometry ─────────────────────────────────────────
#define PM_LCD_H_RES         1024
#define PM_LCD_V_RES         600
#define PM_LCD_BIT_PER_PIXEL 16   // RGB565

// ── MIPI-DSI defaults (ESP32-P4 reference) ───────────────────
// num_data_lanes   = 2 (P4 supports up to 2)
// lane_bit_rate    = 1000 Mbps  (well within 480..1500 envelope)
// pixel clock      = ~52 MHz   (1024 + h_porch * 600 + v_porch * 60Hz)
#define PM_MIPI_LANE_BITRATE_MBPS   1000

// ── Pin map (VERIFIED — ELECROW wiki Lesson 14 + main wiki) ──
//
// MIPI-DSI lanes are routed via P4 dedicated pads — no GPIO
// numbers; the bus driver knows which pads to use. Reference:
//   DATA0=IO40, DATA1=IO39, DATA2=IO36, DATA3=IO35
//   CLKN=IO37,  CLKP=IO38,  REXT=IO34
//
// I2C1 (touch + external connector). Verified from wiki:
#define PM_PIN_I2C_SDA      45
#define PM_PIN_I2C_SCL      46
#define PM_PIN_TOUCH_INT    42    // GT911 INT (level at reset = address)
#define PM_PIN_TOUCH_RST    40    // GT911 RST
#define PM_I2C_FREQ_HZ      400000

// GT911 I2C address depends on INT level at reset:
//   INT held low  → 0x5D
//   INT held high → 0x14
// We hold INT low during reset → 0x5D
#define PM_GT911_ADDR       0x5D

// Backlight — separate power and PWM enable per ELECROW
#define PM_PIN_LCD_BL_PWR   29    // backlight power rail enable
#define PM_PIN_LCD_BL       31    // backlight PWM (LEDC)
#define PM_LCD_BL_LEDC_CH    0
#define PM_LCD_BL_FREQ_HZ    5000

// SD card SDIO 1-bit (P4 has dedicated SDMMC controller)
#define PM_PIN_SD_CLK       43
#define PM_PIN_SD_CMD       44
#define PM_PIN_SD_D0        39

// I2S to NS4168 codec — verified from Lesson 14
#define PM_PIN_I2S_BCK      22    // BCLK (Bit Clock)
#define PM_PIN_I2S_WS       21    // LRCLK (Left-Right Clock)
#define PM_PIN_I2S_DOUT     23    // SDATA (Serial Data)
#define PM_PIN_I2S_AMP_CTRL 30    // amplifier mute/enable
// PDM microphone (separate from speaker I2S):
#define PM_PIN_PDM_MIC_MCLK 24
#define PM_PIN_PDM_MIC_SD   26

// ── Init / control ───────────────────────────────────────────
esp_err_t pm_bsp_init(void);

// LVGL tick task — must be running for animations and timeouts.
// Pinned to Core 1; harmless to call twice (second call no-ops).
void pm_bsp_start_lvgl_tick_task(void);

// Backlight 0..100 percent. 0 = off.
void pm_bsp_set_backlight(uint8_t pct);

// Forward-declared — apps that explicitly need the display
// or input device handles can fetch them, but the registered
// LVGL drivers cover all normal use.
struct _lv_display_t;
struct _lv_indev_t;
struct _lv_display_t* pm_bsp_lvgl_display(void);
struct _lv_indev_t*   pm_bsp_lvgl_touch(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_BSP_H
