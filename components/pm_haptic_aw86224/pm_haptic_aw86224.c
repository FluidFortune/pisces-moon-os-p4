// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md

#include "pm_haptic_aw86224.h"
#include "pm_bsp.h"
#include "pm_board.h"

#include "esp_log.h"

#if PM_BOARD_HAS_NATIVE_HAPTIC

static const char* TAG = "PM_HAPTIC";

// AW86224 register map (from the datasheet; commonly-used subset).
#define REG_ID            0x00   // chip ID (returns 0x24)
#define REG_SYSST         0x01   // system status
#define REG_SYSINT        0x02   // interrupt status
#define REG_SYSCTRL       0x09   // system control
#define REG_PLAYCFG3      0x08   // play config — RAM/RTP mode select
#define REG_PLAYCFG4      0x07   // GO bit + stop
#define REG_WAVCFG1       0x0C   // waveform 1 index
#define REG_WAVCFG2       0x0D
#define REG_WAVCFG3       0x0E
#define REG_WAVCFG4       0x0F
#define REG_WAVCFG5       0x10
#define REG_WAVCFG6       0x11
#define REG_WAVCFG7       0x12
#define REG_WAVCFG8       0x13

#define AW86224_CHIP_ID   0x24

#define GO_BIT            0x01
#define STOP_BIT          0x02

static bool s_present = false;

static esp_err_t _write_reg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { reg, val };
    return pm_bsp_i2c2_transmit(PM_AW86224_ADDR, tx, sizeof(tx), 50);
}

static esp_err_t _read_reg(uint8_t reg, uint8_t* val) {
    return pm_bsp_i2c2_transmit_receive(PM_AW86224_ADDR, &reg, 1, val, 1, 50);
}

esp_err_t pm_haptic_init(void) {
    uint8_t id = 0;
    esp_err_t err = _read_reg(REG_ID, &id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no AW86224 at 0x%02x", PM_AW86224_ADDR);
        s_present = false;
        return ESP_ERR_NOT_FOUND;
    }
    if (id != AW86224_CHIP_ID) {
        ESP_LOGW(TAG, "unexpected chip ID 0x%02x (want 0x%02x)",
                 id, AW86224_CHIP_ID);
        s_present = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Take the chip out of standby (REG_SYSCTRL bit 0 = standby).
    _write_reg(REG_SYSCTRL, 0x00);

    // Select RAM (waveform-library) play mode: PLAYCFG3 bits 1:0 = 00.
    _write_reg(REG_PLAYCFG3, 0x00);

    s_present = true;
    ESP_LOGI(TAG, "AW86224 ready");
    return ESP_OK;
}

bool pm_haptic_present(void) { return s_present; }

esp_err_t pm_haptic_play_raw(uint8_t waveform_index) {
    if (!s_present) return ESP_ERR_NOT_FOUND;
    // Set waveform 1, clear 2..8 (single-effect play).
    esp_err_t err = _write_reg(REG_WAVCFG1, waveform_index);
    if (err != ESP_OK) return err;
    for (uint8_t r = REG_WAVCFG2; r <= REG_WAVCFG8; r++) {
        _write_reg(r, 0x00);
    }
    // Trigger GO bit.
    return _write_reg(REG_PLAYCFG4, GO_BIT);
}

esp_err_t pm_haptic_play(pm_haptic_effect_t effect) {
    return pm_haptic_play_raw((uint8_t)effect);
}

esp_err_t pm_haptic_stop(void) {
    if (!s_present) return ESP_OK;
    return _write_reg(REG_PLAYCFG4, STOP_BIT);
}

#else  // !PM_BOARD_HAS_NATIVE_HAPTIC

esp_err_t pm_haptic_init(void)                     { return ESP_ERR_NOT_SUPPORTED; }
bool      pm_haptic_present(void)                  { return false; }
esp_err_t pm_haptic_play(pm_haptic_effect_t e)     { (void)e; return ESP_OK; }
esp_err_t pm_haptic_play_raw(uint8_t i)            { (void)i; return ESP_OK; }
esp_err_t pm_haptic_stop(void)                     { return ESP_OK; }

#endif
