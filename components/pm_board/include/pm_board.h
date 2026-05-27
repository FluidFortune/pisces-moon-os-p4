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
// Optional: ELECROW CrowPanel Advanced 5" ESP32-P4 HMI, 800x480.
//
// GPS/radio default for both panels is the Cardputer ADV companion on
// the physical UART1 connector (RX1/TX1, P4 IO48/IO47). I2C remains a
// possible low-speed control/probe path. The direct BN-180 UART/IO52 path
// is kept in-tree for bench experiments, but it is not part of normal boot.
//
// Build the 5" profile with:
//   PM_P4_BOARD=elecrow_p4_5 idf.py -B build-p4-5 build

#if defined(PM_BOARD_PROFILE_ELECROW_P4_5)

#define PM_BOARD_NAME                  "ELECROW CrowPanel Advanced 5\" ESP32-P4"
#define PM_BOARD_SHORT_NAME            "P4-5"
#define PM_BOARD_PANEL_DETAIL          "800x480 EK79007"
#define PM_BOARD_LCD_H_RES             800
#define PM_BOARD_LCD_V_RES             480
#define PM_BOARD_MIPI_LANE_BITRATE_MBPS 1000
#define PM_BOARD_DPI_CLK_MHZ           40
#define PM_BOARD_HSYNC_PW              70
#define PM_BOARD_HSYNC_BP              80
#define PM_BOARD_HSYNC_FP              80
#define PM_BOARD_VSYNC_PW              10
#define PM_BOARD_VSYNC_BP              20
#define PM_BOARD_VSYNC_FP              10
// IO52 local GPS is parked; use Cardputer ADV as the UART1 GPS/radio source.
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
#define PM_BOARD_MIPI_LANE_BITRATE_MBPS 1000
#define PM_BOARD_DPI_CLK_MHZ           51
#define PM_BOARD_HSYNC_PW              70
#define PM_BOARD_HSYNC_BP              160
#define PM_BOARD_HSYNC_FP              160
#define PM_BOARD_VSYNC_PW              10
#define PM_BOARD_VSYNC_BP              23
#define PM_BOARD_VSYNC_FP              12
// IO52 local GPS is parked; use Cardputer ADV as the UART1 GPS/radio source.
#define PM_BOARD_LOCAL_GPS_UART        0
#define PM_BOARD_CARDPUTER_RADIO_BRIDGE 1
#define PM_BOARD_CARDPUTER_UART_BRIDGE 1

#endif

#define PM_BOARD_LCD_BIT_PER_PIXEL     16

#endif  // PM_BOARD_H
