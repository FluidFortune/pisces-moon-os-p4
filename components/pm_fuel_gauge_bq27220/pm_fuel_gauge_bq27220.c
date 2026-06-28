// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md

#include "pm_fuel_gauge_bq27220.h"
#include "pm_bsp.h"

#include "esp_log.h"
#include <string.h>

static const char* TAG = "PM_BQ27220";

// Standard data commands (TI BQ27220 TRM §3.1). Each command is a
// 1-byte register address; reading 2 bytes returns the data
// little-endian. The chip ignores writes to data commands.
#define CMD_CONTROL                 0x00
#define CMD_AT_RATE                 0x02
#define CMD_AT_RATE_TIME_TO_EMPTY   0x04
#define CMD_TEMPERATURE             0x06   // 0.1 K
#define CMD_VOLTAGE                 0x08   // mV
#define CMD_BATTERY_STATUS          0x0A
#define CMD_CURRENT                 0x0C   // mA (signed)
#define CMD_REMAINING_CAPACITY      0x10   // mAh
#define CMD_FULL_CHARGE_CAPACITY    0x12   // mAh
#define CMD_STATE_OF_CHARGE         0x2C   // %

// CMD_BATTERY_STATUS bits we care about
#define BSTAT_DSG       (1u << 6)   // 1 = discharging
#define BSTAT_FC        (1u << 5)   // fully charged
#define BSTAT_BAT_DET   (1u << 3)   // battery detected

static bool s_present = false;

static esp_err_t _read16(uint8_t cmd, uint16_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    uint8_t buf[2] = {0};
    esp_err_t err = pm_bsp_i2c_transmit_receive(PM_BQ27220_ADDR, &cmd, 1,
                                                 buf, sizeof(buf), 50);
    if (err != ESP_OK) return err;
    *out = (uint16_t)(buf[0] | (buf[1] << 8));
    return ESP_OK;
}

esp_err_t pm_fuel_gauge_init(void) {
    uint16_t v = 0;
    esp_err_t err = _read16(CMD_VOLTAGE, &v);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no response at 0x%02x — fuel gauge absent or off",
                 PM_BQ27220_ADDR);
        s_present = false;
        return ESP_ERR_NOT_FOUND;
    }
    s_present = true;
    ESP_LOGI(TAG, "BQ27220 present, battery V=%u mV", v);
    return ESP_OK;
}

bool pm_fuel_gauge_present(void) { return s_present; }

esp_err_t pm_fuel_gauge_read(pm_battery_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!s_present) return ESP_ERR_NOT_FOUND;

    uint16_t v = 0, soc = 0, rem = 0, full = 0, status = 0, temp = 0;
    uint16_t cur_raw = 0;

    if (_read16(CMD_VOLTAGE,              &v)       != ESP_OK) return ESP_FAIL;
    if (_read16(CMD_STATE_OF_CHARGE,      &soc)     != ESP_OK) return ESP_FAIL;
    if (_read16(CMD_CURRENT,              &cur_raw) != ESP_OK) return ESP_FAIL;
    if (_read16(CMD_REMAINING_CAPACITY,   &rem)     != ESP_OK) return ESP_FAIL;
    if (_read16(CMD_FULL_CHARGE_CAPACITY, &full)    != ESP_OK) return ESP_FAIL;
    if (_read16(CMD_BATTERY_STATUS,       &status)  != ESP_OK) return ESP_FAIL;
    if (_read16(CMD_TEMPERATURE,          &temp)    != ESP_OK) return ESP_FAIL;

    out->voltage_mv        = v;
    out->soc_pct           = (uint8_t)(soc > 100 ? 100 : soc);
    out->current_ma        = (int16_t)cur_raw;   // already two's complement
    out->remaining_mah     = rem;
    out->full_capacity_mah = full;
    out->discharging       = (status & BSTAT_DSG) != 0;
    out->charging          = !out->discharging && (out->current_ma > 0);
    out->fully_charged     = (status & BSTAT_FC) != 0;
    out->present           = (status & BSTAT_BAT_DET) != 0;
    // Temperature: 0.1 K → tenths of degrees C (subtract 273.15 K ≈ 2731.5)
    out->temperature_c10   = (int16_t)((int32_t)temp - 2732);
    return ESP_OK;
}
