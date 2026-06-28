// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_BOARD_H
#define PM_BOARD_H

// Compile-time board profile.
//
// Default: ELECROW CrowPanel Advanced 7" ESP32-P4 HMI, 1024x600.
// Optional: ELECROW CrowPanel Advanced 5" ESP32-P4 HMI,  800x480.
// Optional: LilyGO T-Display-P4, 540x1168 portrait MIPI-DSI.
//
// Profiles are selected at idf.py invocation, e.g.:
//   PM_P4_BOARD=elecrow_p4_7      idf.py -B build-p4-7      build
//   PM_P4_BOARD=elecrow_p4_5      idf.py -B build-p4-5      build
//   PM_P4_BOARD=lilygo_tdisplay_p4 idf.py -B build-tdisplay build
//
// Pisces Moon OS is the OS for the P4/C6 device class — wherever
// the chipset shows up, a board profile + BSP file pair brings it
// up. The launcher, peer registry, ESP-Hosted plumbing, and app
// catalog are board-agnostic.

#if defined(PM_BOARD_PROFILE_LILYGO_TDISPLAY_P4)

// LilyGO T-Display-P4: integrated portable device with native LoRa
// (SX1262), GPS (L76K), audio (ES8311 + NS4150B + mic), haptic
// (AW86224 LRA), camera (OV2710), RTC (PCF8563), fuel gauge
// (BQ27220), IMU (ICM20948), and XL9535 power-management expander
// (the same pattern as the T-LoraPager S3 device). Same C6-on-SDIO
// auxiliary radio as the Elecrow CrowPanel, so ESP-Hosted plumbing
// is fully reusable.
//
// Display: HI8561 4.05" 540x1168 portrait MIPI-DSI (default).
//          (Alt RM69A10 4.1" AMOLED 568x1232 is a build option;
//          this profile targets the default IPS panel.)
#define PM_BOARD_NAME                  "LilyGO T-Display-P4"
#define PM_BOARD_SHORT_NAME            "TDISP-P4"
#define PM_BOARD_PANEL_DETAIL          "540x1168 HI8561 portrait MIPI"
#define PM_BOARD_LCD_H_RES             540
#define PM_BOARD_LCD_V_RES             1168
#define PM_BOARD_LCD_PORTRAIT          1
#define PM_BOARD_MIPI_LANE_BITRATE_MBPS 1000
#define PM_BOARD_MIPI_DATA_LANE_NUM    2
#define PM_BOARD_DPI_CLK_MHZ           60
#define PM_BOARD_HSYNC_PW              28
#define PM_BOARD_HSYNC_BP              26
#define PM_BOARD_HSYNC_FP              20
#define PM_BOARD_VSYNC_PW              2
#define PM_BOARD_VSYNC_BP              22
#define PM_BOARD_VSYNC_FP              200

// L76K GPS is wired directly to the P4 on UART2 (GPIO 22/23) at
// 9600 baud (L76K factory default). Cardputer remains an optional
// secondary radio bridge over the EXT_1X4P_1 header (UART1, GPIO
// 47/48). C5 lives on the EXT_1X4P_2 header (UART, GPIO 45/46).
#define PM_BOARD_LOCAL_GPS_UART        1
#define PM_BOARD_LOCAL_GPS_RX_PIN      22
#define PM_BOARD_LOCAL_GPS_TX_PIN      23
#define PM_BOARD_LOCAL_GPS_BAUD        9600
#define PM_BOARD_CARDPUTER_RADIO_BRIDGE 1
#define PM_BOARD_CARDPUTER_UART_BRIDGE 1

// XL9535 power-management gating is active on this device.
// Components that need a specific peripheral powered call into
// pm_xl9535 to enable it before use.
#define PM_BOARD_HAS_XL9535            1
#define PM_BOARD_XL9535_I2C_ADDR       0x20

// On-board native peripherals discoverable via pm_peer at boot.
#define PM_BOARD_HAS_NATIVE_LORA       1   // HPD16A (SX1262)
#define PM_BOARD_HAS_NATIVE_GPS        1   // L76K
#define PM_BOARD_HAS_NATIVE_AUDIO      1   // ES8311 + NS4150B + mic
#define PM_BOARD_HAS_NATIVE_HAPTIC     1   // AW86224 LRA driver
#define PM_BOARD_HAS_NATIVE_RTC        1   // PCF8563
#define PM_BOARD_HAS_NATIVE_FUEL_GAUGE 1   // BQ27220
#define PM_BOARD_HAS_NATIVE_IMU        1   // ICM20948
#define PM_BOARD_HAS_NATIVE_CAMERA     1   // OV2710 MIPI-CSI
#define PM_BOARD_HAS_USB_HOST          1   // USB-A receptacle
#define PM_BOARD_HAS_ETHERNET          1   // RMII PHY

#elif defined(PM_BOARD_PROFILE_ELECROW_P4_5)

#define PM_BOARD_NAME                  "ELECROW CrowPanel Advanced 5\" ESP32-P4"
#define PM_BOARD_SHORT_NAME            "P4-5"
#define PM_BOARD_PANEL_DETAIL          "800x480 EK79007"
#define PM_BOARD_LCD_H_RES             800
#define PM_BOARD_LCD_V_RES             480
#define PM_BOARD_LCD_PORTRAIT          0
#define PM_BOARD_MIPI_LANE_BITRATE_MBPS 1000
#define PM_BOARD_MIPI_DATA_LANE_NUM    2
#define PM_BOARD_DPI_CLK_MHZ           40
#define PM_BOARD_HSYNC_PW              70
#define PM_BOARD_HSYNC_BP              80
#define PM_BOARD_HSYNC_FP              80
#define PM_BOARD_VSYNC_PW              10
#define PM_BOARD_VSYNC_BP              20
#define PM_BOARD_VSYNC_FP              10
#define PM_BOARD_LOCAL_GPS_UART        0
#define PM_BOARD_CARDPUTER_RADIO_BRIDGE 1
#define PM_BOARD_CARDPUTER_UART_BRIDGE 1

#else

#define PM_BOARD_PROFILE_ELECROW_P4_7  1
#define PM_BOARD_NAME                  "ELECROW CrowPanel Advanced 7\" ESP32-P4"
#define PM_BOARD_SHORT_NAME            "P4-7"
#define PM_BOARD_PANEL_DETAIL          "1024x600 EK79007"
#define PM_BOARD_LCD_H_RES             1024
#define PM_BOARD_LCD_V_RES             600
#define PM_BOARD_LCD_PORTRAIT          0
#define PM_BOARD_MIPI_LANE_BITRATE_MBPS 1000
#define PM_BOARD_MIPI_DATA_LANE_NUM    2
#define PM_BOARD_DPI_CLK_MHZ           51
#define PM_BOARD_HSYNC_PW              70
#define PM_BOARD_HSYNC_BP              160
#define PM_BOARD_HSYNC_FP              160
#define PM_BOARD_VSYNC_PW              10
#define PM_BOARD_VSYNC_BP              23
#define PM_BOARD_VSYNC_FP              12
#define PM_BOARD_LOCAL_GPS_UART        0
#define PM_BOARD_CARDPUTER_RADIO_BRIDGE 1
#define PM_BOARD_CARDPUTER_UART_BRIDGE 1

#endif

// Default any optional capability flags that aren't set above to 0
// so consumers can test them unconditionally with #if.
#ifndef PM_BOARD_HAS_XL9535
#define PM_BOARD_HAS_XL9535            0
#endif
#ifndef PM_BOARD_HAS_NATIVE_LORA
#define PM_BOARD_HAS_NATIVE_LORA       0
#endif
#ifndef PM_BOARD_HAS_NATIVE_GPS
#define PM_BOARD_HAS_NATIVE_GPS        0
#endif
#ifndef PM_BOARD_HAS_NATIVE_AUDIO
#define PM_BOARD_HAS_NATIVE_AUDIO      0
#endif
#ifndef PM_BOARD_HAS_NATIVE_HAPTIC
#define PM_BOARD_HAS_NATIVE_HAPTIC     0
#endif
#ifndef PM_BOARD_HAS_NATIVE_RTC
#define PM_BOARD_HAS_NATIVE_RTC        0
#endif
#ifndef PM_BOARD_HAS_NATIVE_FUEL_GAUGE
#define PM_BOARD_HAS_NATIVE_FUEL_GAUGE 0
#endif
#ifndef PM_BOARD_HAS_NATIVE_IMU
#define PM_BOARD_HAS_NATIVE_IMU        0
#endif
#ifndef PM_BOARD_HAS_NATIVE_CAMERA
#define PM_BOARD_HAS_NATIVE_CAMERA     0
#endif
#ifndef PM_BOARD_HAS_USB_HOST
#define PM_BOARD_HAS_USB_HOST          0
#endif
#ifndef PM_BOARD_HAS_ETHERNET
#define PM_BOARD_HAS_ETHERNET          0
#endif

#define PM_BOARD_LCD_BIT_PER_PIXEL     16

#endif  // PM_BOARD_H
