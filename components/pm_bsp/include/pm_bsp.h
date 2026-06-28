// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_bsp.h — Board Support Package public interface
//
//  Pisces Moon OS targets the ESP32-P4/C6 device class. Each
//  supported board ships a profile in pm_board.h plus a
//  matching BSP source file under this component; the public
//  surface declared here is identical across boards.
//
//  Currently supported board profiles:
//    PM_BOARD_PROFILE_ELECROW_P4_7        (default)
//    PM_BOARD_PROFILE_ELECROW_P4_5
//    PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4
//
//  Shape:
//    pm_bsp_init()                  — bring up display, touch,
//                                     SD, audio, I2C buses,
//                                     power gating; called once
//                                     in app_main before launcher
//    pm_bsp_start_lvgl_tick_task()  — start LVGL tick on Core 1
//    pm_bsp_set_backlight(0..100)   — display brightness
//    pm_bsp_i2c_transmit / *_receive — shared bus 1 helper for
//                                     low-duty I2C peers (peripherals
//                                     register their own clients
//                                     when they need higher cadence)
//
//  Pin defines below are conditional on the active board profile.
//  Per-board specifics that don't have a public API are kept
//  inside the matching .c file.
// ============================================================

#ifndef PM_BSP_H
#define PM_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "pm_board.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Display geometry (board-agnostic) ────────────────────────
#define PM_LCD_H_RES         PM_BOARD_LCD_H_RES
#define PM_LCD_V_RES         PM_BOARD_LCD_V_RES
#define PM_LCD_BIT_PER_PIXEL PM_BOARD_LCD_BIT_PER_PIXEL   // RGB565

// ── MIPI-DSI lane bit rate ───────────────────────────────────
#define PM_MIPI_LANE_BITRATE_MBPS   PM_BOARD_MIPI_LANE_BITRATE_MBPS

// ============================================================
//  Board-conditional pin map
// ============================================================
#if defined(PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4)

// ── LilyGO T-Display-P4 ──────────────────────────────────────
//
// Per components/private_library/t_display_p4_config.h in the
// LilyGO repo. Touch is on the same I2C bus as the XL9535 power
// expander, the PCF8563 RTC, and the BQ27220 fuel gauge — keep
// touch reads brief or use the dedicated I2C client owned by the
// touch driver.
//
// MIPI-DSI lanes are routed via P4 dedicated pads (no GPIO
// numbers needed; the bus driver knows the pads).

// I2C1 — touch, XL9535, PCF8563 RTC, BQ27220 fuel gauge
#define PM_PIN_I2C_SDA          7
#define PM_PIN_I2C_SCL          8
#define PM_I2C_FREQ_HZ          400000

// I2C2 — ES8311 codec, AW86224 haptic, ICM20948 IMU, OV2710 camera
#define PM_PIN_I2C2_SDA         20
#define PM_PIN_I2C2_SCL         21
#define PM_I2C2_FREQ_HZ         400000

// Touch is integrated with the HI8561 display controller and
// reached at IIC address 0x68 over I2C1. Reset and INT lines are
// gated through the XL9535 expander (IO3 = RST, IO4 = INT).
#define PM_HI8561_TOUCH_ADDR    0x68
// Marker — the touch reset/INT lines are XL9535-side, not P4 GPIO.
#define PM_PIN_TOUCH_INT        -1
#define PM_PIN_TOUCH_RST        -1

// Backlight — direct PWM, no separate power rail GPIO
#define PM_PIN_LCD_BL_PWR       -1   // n/a; backlight power is gated via XL9535
#define PM_PIN_LCD_BL           51
#define PM_LCD_BL_LEDC_CH        0
#define PM_LCD_BL_FREQ_HZ        5000

// SD card on SDMMC — 4-bit interface
#define PM_PIN_SD_CLK           43
#define PM_PIN_SD_CMD           44
#define PM_PIN_SD_D0            39
#define PM_PIN_SD_D1            40
#define PM_PIN_SD_D2            41
#define PM_PIN_SD_D3            42
#define PM_SD_BUS_WIDTH         4

// ES8311 codec I2S (BCLK / MCLK / WS / DAC / ADC)
#define PM_PIN_I2S_MCLK         13
#define PM_PIN_I2S_BCK          12
#define PM_PIN_I2S_WS           9
#define PM_PIN_I2S_DOUT         10    // DAC out (host → codec → speaker)
#define PM_PIN_I2S_DIN          11    // ADC in  (mic → codec → host)
#define PM_PIN_I2S_AMP_CTRL     -1    // NS4150B mute via codec, no separate pin

// SX1262 LoRa on SPI1
#define PM_PIN_SPI1_SCLK        2
#define PM_PIN_SPI1_MOSI        3
#define PM_PIN_SPI1_MISO        4
#define PM_PIN_SX1262_CS        24
#define PM_PIN_SX1262_BUSY      6

// L76K GPS on direct UART2
#define PM_PIN_GPS_RX           PM_BOARD_LOCAL_GPS_RX_PIN   // 22
#define PM_PIN_GPS_TX           PM_BOARD_LOCAL_GPS_TX_PIN   // 23

// External 1×4 headers (peer-port pinout matches Elecrow Grove
// pattern intentionally — the C5 UART defaults at 45/46 work
// unchanged on this device).
#define PM_PIN_EXT_HEADER1_A    47   // historically Cardputer UART1 RX
#define PM_PIN_EXT_HEADER1_B    48   // historically Cardputer UART1 TX
#define PM_PIN_EXT_HEADER2_A    45   // C5 UART RX (pm_c5_uart default)
#define PM_PIN_EXT_HEADER2_B    46   // C5 UART TX (pm_c5_uart default)

// XL9535 power-expander internal pin assignments — used by the
// pm_xl9535 driver to know which expander IO controls which rail.
// Names match the LilyGO config header.
#define PM_XL9535_3V3_PWR_EN        0
#define PM_XL9535_SKY13453_VCTL     1
#define PM_XL9535_SCREEN_RST        2
#define PM_XL9535_TOUCH_RST         3
#define PM_XL9535_TOUCH_INT         4
#define PM_XL9535_ETHERNET_RST      5
#define PM_XL9535_5V0_PWR_EN        6
#define PM_XL9535_EXT_SENSOR_INT    7
#define PM_XL9535_VCCA_PWR_EN       10
#define PM_XL9535_GPS_WAKE_UP       11
#define PM_XL9535_RTC_INT           12
#define PM_XL9535_C6_WAKE_UP        13
#define PM_XL9535_C6_EN             14
#define PM_XL9535_SD_EN             15
#define PM_XL9535_SX1262_RST        16
#define PM_XL9535_SX1262_DIO1       17

#else  // ─── Elecrow CrowPanel profiles (P4-7 default + P4-5) ───

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
#define PM_SD_BUS_WIDTH     1

// I2S to NS4168 codec — verified from Lesson 14
#define PM_PIN_I2S_BCK      22    // BCLK (Bit Clock)
#define PM_PIN_I2S_WS       21    // LRCLK (Left-Right Clock)
#define PM_PIN_I2S_DOUT     23    // SDATA (Serial Data)
#define PM_PIN_I2S_AMP_CTRL 30    // amplifier mute/enable
// PDM microphone (separate from speaker I2S):
#define PM_PIN_PDM_MIC_MCLK 24
#define PM_PIN_PDM_MIC_SD   26

#endif  // board profile

// ── Init / control (board-agnostic API) ──────────────────────
// On boards with an external power-management expander (XL9535
// on the LilyGO), boot order is:
//   1. pm_bsp_init_buses()       — brings up I2C peripherals
//   2. pm_xl9535_init() + boot   — powers on rails and releases
//                                  resets (LilyGO only)
//   3. pm_bsp_init()             — display, touch, SD, LVGL
//
// On boards without an expander (Elecrow), pm_bsp_init() does
// all three steps itself; callers can skip pm_bsp_init_buses().
esp_err_t pm_bsp_init_buses(void);
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

// Shared I2C1 bus helper for low-duty modular peers. On Elecrow
// boards this also serves the GT911 touch controller; on LilyGO
// it serves the XL9535 + RTC + fuel gauge. Either way, keep
// transactions short — the touch / power driver owns the higher-
// cadence traffic.
esp_err_t pm_bsp_i2c_transmit(uint8_t addr,
                              const uint8_t* tx, size_t tx_len,
                              int timeout_ms);
esp_err_t pm_bsp_i2c_transmit_receive(uint8_t addr,
                                      const uint8_t* tx, size_t tx_len,
                                      uint8_t* rx, size_t rx_len,
                                      int timeout_ms);

// LilyGO-only: shared I2C2 helper for audio, haptic, IMU, camera.
#if defined(PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4)
esp_err_t pm_bsp_i2c2_transmit(uint8_t addr,
                               const uint8_t* tx, size_t tx_len,
                               int timeout_ms);
esp_err_t pm_bsp_i2c2_transmit_receive(uint8_t addr,
                                       const uint8_t* tx, size_t tx_len,
                                       uint8_t* rx, size_t rx_len,
                                       int timeout_ms);
#endif

#ifdef __cplusplus
}
#endif

#endif  // PM_BSP_H
