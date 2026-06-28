// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_IMU_ICM20948_H
#define PM_IMU_ICM20948_H

// ============================================================
//  pm_imu_icm20948.h — TDK InvenSense ICM-20948 9-axis IMU
//
//  3-axis accel + 3-axis gyro + 3-axis magnetometer (the mag
//  is the AK09916 sub-device, reached via the ICM's I2C master).
//  Lives at I2C address 0x68 on the LilyGO T-Display-P4's
//  I2C2 bus.
//
//  Pisces Moon uses it for:
//    - Auto-rotation (landscape ↔ portrait based on gravity)
//    - Bump / drop detection (wake from sleep on a tap)
//    - Heading for navigation apps and the tilemap compass arrow
//    - Step counting / activity for the calling-card mode
//
//  This driver exposes the basic accel + gyro path at reasonable
//  defaults (±4g, ±500 dps, 100 Hz). Magnetometer init is
//  separately enabled because it requires routing through the
//  ICM's auxiliary I2C bus — apps that need it call
//  pm_imu_enable_mag().
//
//  Datasheets: InvenSense DS-000189 (ICM-20948), AKM AK09916C.
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_ICM20948_ADDR  0x68

typedef struct {
    float ax_g;     // accel in g
    float ay_g;
    float az_g;
    float gx_dps;   // gyro in degrees-per-second
    float gy_dps;
    float gz_dps;
    bool  mag_valid;
    float mx_uT;    // magnetometer in microtesla (only if enabled)
    float my_uT;
    float mz_uT;
    float temp_c;   // die temperature
} pm_imu_sample_t;

esp_err_t pm_imu_init(void);
bool      pm_imu_present(void);

// Single-shot sample. Returns ESP_OK + populated struct, or
// ESP_ERR_NOT_FOUND if the IMU isn't initialised.
esp_err_t pm_imu_read(pm_imu_sample_t* out);

// Enable the auxiliary AK09916 magnetometer path. Optional;
// adds a small amount of recurring I2C traffic from the ICM
// to its slave. Returns ESP_ERR_NOT_FOUND if init hasn't
// succeeded, ESP_FAIL if the mag doesn't respond.
esp_err_t pm_imu_enable_mag(void);

// Convenience for the auto-rotation logic: returns the dominant
// gravity axis as a screen orientation hint.
typedef enum {
    PM_ORIENT_PORTRAIT_UP    = 0,
    PM_ORIENT_PORTRAIT_DOWN  = 1,
    PM_ORIENT_LANDSCAPE_LEFT = 2,
    PM_ORIENT_LANDSCAPE_RIGHT= 3,
    PM_ORIENT_FACE_UP        = 4,
    PM_ORIENT_FACE_DOWN      = 5,
} pm_orientation_t;
esp_err_t pm_imu_get_orientation(pm_orientation_t* out);

#ifdef __cplusplus
}
#endif

#endif
