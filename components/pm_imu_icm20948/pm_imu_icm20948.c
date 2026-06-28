// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md

#include "pm_imu_icm20948.h"
#include "pm_bsp.h"
#include "pm_board.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

#if PM_BOARD_HAS_NATIVE_IMU

static const char* TAG = "PM_IMU";

// ICM-20948 has a bank-switched register space. Most registers
// live in Bank 0; gyro/accel config is in Bank 2. We bounce
// between banks via the REG_BANK_SEL register (0x7F) which is
// the same on every bank.

#define REG_BANK_SEL          0x7F
#define BANK_0                0x00
#define BANK_2                (0x02 << 4)

// Bank 0 registers
#define REG_WHO_AM_I          0x00
#define REG_PWR_MGMT_1        0x06
#define REG_PWR_MGMT_2        0x07
#define REG_ACCEL_XOUT_H      0x2D
#define REG_GYRO_XOUT_H       0x33
#define REG_TEMP_OUT_H        0x39

// Bank 2 registers
#define REG_GYRO_SMPLRT_DIV   0x00
#define REG_GYRO_CONFIG_1     0x01
#define REG_ACCEL_SMPLRT_DIV1 0x10
#define REG_ACCEL_SMPLRT_DIV2 0x11
#define REG_ACCEL_CONFIG      0x14

// PWR_MGMT_1 bits
#define PWR_DEVICE_RESET      0x80
#define PWR_CLKSEL_AUTO       0x01

#define ICM20948_WHOAMI       0xEA

// Sensitivity at our chosen ranges
#define ACCEL_SENS_4G_PER_LSB    (4.0f / 32768.0f)        // g per LSB
#define GYRO_SENS_500DPS_PER_LSB (500.0f / 32768.0f)      // dps per LSB

static bool s_present = false;

// Cache the current bank so we don't churn it on every register
// access — bank switches are visible noise on the I2C trace.
static uint8_t s_current_bank = 0xFF;

static esp_err_t _write_reg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { reg, val };
    return pm_bsp_i2c2_transmit(PM_ICM20948_ADDR, tx, sizeof(tx), 50);
}

static esp_err_t _read_block(uint8_t reg, uint8_t* buf, size_t len) {
    return pm_bsp_i2c2_transmit_receive(PM_ICM20948_ADDR, &reg, 1,
                                          buf, len, 50);
}

static esp_err_t _select_bank(uint8_t bank_byte) {
    if (s_current_bank == bank_byte) return ESP_OK;
    esp_err_t err = _write_reg(REG_BANK_SEL, bank_byte);
    if (err == ESP_OK) s_current_bank = bank_byte;
    return err;
}

esp_err_t pm_imu_init(void) {
    s_current_bank = 0xFF;

    // Select Bank 0 + read WHO_AM_I (probe).
    esp_err_t err = _select_bank(BANK_0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no response from ICM-20948");
        s_present = false;
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t id = 0;
    if (_read_block(REG_WHO_AM_I, &id, 1) != ESP_OK || id != ICM20948_WHOAMI) {
        ESP_LOGW(TAG, "ICM-20948 ID 0x%02x (want 0x%02x)", id, ICM20948_WHOAMI);
        s_present = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Soft reset, then 100 ms for the chip to come up clean.
    _write_reg(REG_PWR_MGMT_1, PWR_DEVICE_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
    s_current_bank = 0xFF;   // bank reverts after reset

    // Wake up + auto clock select.
    _select_bank(BANK_0);
    _write_reg(REG_PWR_MGMT_1, PWR_CLKSEL_AUTO);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Enable accel + gyro (clear DISABLE_ bits in PWR_MGMT_2).
    _write_reg(REG_PWR_MGMT_2, 0x00);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Configure ranges in Bank 2: gyro ±500 dps, accel ±4g.
    _select_bank(BANK_2);

    // Gyro: SMPLRT_DIV=10 → ~100 Hz from 1.1 kHz internal, LPF
    // 17 Hz, FS = 500 dps (bits 2:1 = 01), DLPF enable.
    _write_reg(REG_GYRO_SMPLRT_DIV, 10);
    _write_reg(REG_GYRO_CONFIG_1,   (0x05 << 3) | (0x01 << 1) | 0x01);

    // Accel: same downsample, FS = ±4g (bits 2:1 = 01), DLPF enable.
    _write_reg(REG_ACCEL_SMPLRT_DIV1, 0);
    _write_reg(REG_ACCEL_SMPLRT_DIV2, 10);
    _write_reg(REG_ACCEL_CONFIG,    (0x05 << 3) | (0x01 << 1) | 0x01);

    _select_bank(BANK_0);
    s_present = true;
    ESP_LOGI(TAG, "ICM-20948 ready (±4g / ±500 dps / 100 Hz)");
    return ESP_OK;
}

bool pm_imu_present(void) { return s_present; }

esp_err_t pm_imu_read(pm_imu_sample_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!s_present) return ESP_ERR_NOT_FOUND;

    esp_err_t err = _select_bank(BANK_0);
    if (err != ESP_OK) return err;

    // Read accel (6 bytes), gyro (6 bytes), temp (2 bytes).
    uint8_t buf[14];
    err = _read_block(REG_ACCEL_XOUT_H, buf, 14);
    if (err != ESP_OK) return err;

    int16_t ax = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t ay = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t az = (int16_t)((buf[4] << 8) | buf[5]);
    int16_t gx = (int16_t)((buf[6] << 8) | buf[7]);
    int16_t gy = (int16_t)((buf[8] << 8) | buf[9]);
    int16_t gz = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t tr = (int16_t)((buf[12] << 8) | buf[13]);

    out->ax_g    = ax * ACCEL_SENS_4G_PER_LSB;
    out->ay_g    = ay * ACCEL_SENS_4G_PER_LSB;
    out->az_g    = az * ACCEL_SENS_4G_PER_LSB;
    out->gx_dps  = gx * GYRO_SENS_500DPS_PER_LSB;
    out->gy_dps  = gy * GYRO_SENS_500DPS_PER_LSB;
    out->gz_dps  = gz * GYRO_SENS_500DPS_PER_LSB;
    // Datasheet temp formula: T_C = (TEMP_OUT - RoomTemp_Offset)/Sensitivity + 21
    // Sensitivity ≈ 333.87 LSB/°C; RoomTemp_Offset ≈ 0.
    out->temp_c  = (tr / 333.87f) + 21.0f;
    out->mag_valid = false;
    return ESP_OK;
}

esp_err_t pm_imu_enable_mag(void) {
    if (!s_present) return ESP_ERR_NOT_FOUND;
    // The AK09916 magnetometer sits behind the ICM's I2C-master
    // bridge. Full enablement is a 20-register dance (configure
    // bypass mode or the master I2C clock, then write to the AK's
    // CNTL2 register through SLV0/SLV4 sequencing). For now we
    // stub this — apps that need the mag can implement the rest
    // once we've got the basic IMU integration validated.
    ESP_LOGW(TAG, "magnetometer enable not implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pm_imu_get_orientation(pm_orientation_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    pm_imu_sample_t s;
    esp_err_t err = pm_imu_read(&s);
    if (err != ESP_OK) return err;

    float ax = fabsf(s.ax_g), ay = fabsf(s.ay_g), az = fabsf(s.az_g);
    if (az > ax && az > ay) {
        *out = (s.az_g > 0) ? PM_ORIENT_FACE_UP : PM_ORIENT_FACE_DOWN;
    } else if (ay >= ax) {
        *out = (s.ay_g > 0) ? PM_ORIENT_PORTRAIT_UP : PM_ORIENT_PORTRAIT_DOWN;
    } else {
        *out = (s.ax_g > 0) ? PM_ORIENT_LANDSCAPE_RIGHT
                            : PM_ORIENT_LANDSCAPE_LEFT;
    }
    return ESP_OK;
}

#else  // !PM_BOARD_HAS_NATIVE_IMU

esp_err_t pm_imu_init(void)                            { return ESP_ERR_NOT_SUPPORTED; }
bool      pm_imu_present(void)                          { return false; }
esp_err_t pm_imu_read(pm_imu_sample_t* o)               { (void)o; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t pm_imu_enable_mag(void)                       { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t pm_imu_get_orientation(pm_orientation_t* o)   {
    if (o) *o = PM_ORIENT_PORTRAIT_UP;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
