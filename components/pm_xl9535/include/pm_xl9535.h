// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_XL9535_H
#define PM_XL9535_H

// ============================================================
//  pm_xl9535.h — XL9535 I2C GPIO expander / power gate driver
//
//  The XL9535 (16-pin I2C GPIO expander, like the PCA9555) is
//  used on the LilyGO T-Display-P4 as the master power-rail and
//  peripheral-reset controller — every meaningful peripheral
//  (LoRa, GPS, SD, C6, screen, touch, Ethernet, the 3V3 and 5V
//  rails themselves) sits behind one of its 16 outputs.
//
//  This driver is direct-write register access, mirroring the
//  T-LoraPager XL9555 pattern in spi_treaty.h. No library
//  dependency, no high-level GPIO abstraction.
//
//  Boot ordering is critical and lives in pm_xl9535_boot_sequence().
//  SD comes up first (before pm_hal filesystem init), then the
//  ESP32-C6 enable line (before ESP-Hosted), then the screen,
//  then sensors. The sequence is idempotent — second calls
//  re-assert known state without harm.
//
//  Register map (XL9535, datasheet):
//    0x00 Input  Port 0   (read-only)
//    0x01 Input  Port 1
//    0x02 Output Port 0   ← we write peripheral enables here
//    0x03 Output Port 1
//    0x04 Polarity Inv 0  (unused — we keep polarity natural)
//    0x05 Polarity Inv 1
//    0x06 Config 0        ← 0 = output (we set all bits)
//    0x07 Config 1
//
//  Pin numbering matches the LilyGO config header:
//    IO0..IO7  → bits 0..7 of Port 0
//    IO10..IO17→ bits 0..7 of Port 1
//
//  Public bit constants are defined in pm_bsp.h
//  (PM_XL9535_3V3_PWR_EN, PM_XL9535_LORA_RST, etc.) so callers
//  don't need to know the wiring.
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the expander — sets every pin to output mode and
// drives the safe-default state (all peripherals OFF except the
// rails the device cannot boot without). Called from pm_bsp_init
// on boards that have an XL9535.
esp_err_t pm_xl9535_init(void);

// Run the peripheral boot sequence — power rails up in order,
// peripherals enabled, settle delays observed. Called from
// pm_bsp_init after pm_xl9535_init.
esp_err_t pm_xl9535_boot_sequence(void);

// Set a single expander output. `pin` is the LilyGO IO number
// (0..7 for Port 0, 10..17 for Port 1; pins 8/9 don't exist on
// the package). `on` drives high; off drives low.
esp_err_t pm_xl9535_set(int pin, bool on);

// Read a single expander input.
esp_err_t pm_xl9535_get(int pin, bool* out);

// Strobe a reset line (drives low → delay → drives high).
// Useful for the SX1262_RST, SCREEN_RST, etc. lines.
esp_err_t pm_xl9535_pulse_reset(int pin, uint32_t low_ms);

#ifdef __cplusplus
}
#endif

#endif  // PM_XL9535_H
