// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md

#include "pm_rtc_pcf8563.h"
#include "pm_bsp.h"

#include "esp_log.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char* TAG = "PM_RTC_PCF8563";

// PCF8563 register map
#define REG_CTRL1      0x00
#define REG_CTRL2      0x01
#define REG_SECONDS    0x02   // VL flag in bit 7
#define REG_MINUTES    0x03
#define REG_HOURS      0x04
#define REG_DAYS       0x05
#define REG_WEEKDAYS   0x06
#define REG_MONTHS     0x07   // century flag in bit 7
#define REG_YEARS      0x08

#define VL_FLAG_MASK   0x80
#define CENTURY_MASK   0x80   // 0 = 20xx, 1 = 19xx (we ignore — clamp to 20xx)

static uint8_t _bcd_to_bin(uint8_t v) {
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

static uint8_t _bin_to_bcd(uint8_t v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static esp_err_t _read_block(uint8_t reg, uint8_t* buf, size_t len) {
    return pm_bsp_i2c_transmit_receive(PM_PCF8563_ADDR, &reg, 1,
                                         buf, len, 50);
}

static esp_err_t _write_block(uint8_t reg, const uint8_t* buf, size_t len) {
    if (len + 1 > 16) return ESP_ERR_INVALID_SIZE;
    uint8_t tx[16];
    tx[0] = reg;
    memcpy(&tx[1], buf, len);
    return pm_bsp_i2c_transmit(PM_PCF8563_ADDR, tx, len + 1, 50);
}

esp_err_t pm_rtc_pcf8563_init(bool* clock_was_lost) {
    // Clear control registers (no test mode, no alarm interrupts,
    // no timer interrupts — straight wall-clock operation).
    uint8_t ctrl[2] = { 0x00, 0x00 };
    esp_err_t err = _write_block(REG_CTRL1, ctrl, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ctrl write failed: %s", esp_err_to_name(err));
        return err;
    }

    // Read the seconds register to inspect the VL flag.
    uint8_t sec_raw = 0;
    err = _read_block(REG_SECONDS, &sec_raw, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "seconds read failed: %s", esp_err_to_name(err));
        return err;
    }
    bool lost = (sec_raw & VL_FLAG_MASK) != 0;
    if (clock_was_lost) *clock_was_lost = lost;
    if (lost) {
        ESP_LOGW(TAG, "PCF8563 VL flag set — clock lost time, needs sync");
    } else {
        ESP_LOGI(TAG, "PCF8563 initialised, clock running");
    }
    return ESP_OK;
}

esp_err_t pm_rtc_pcf8563_read(pm_rtc_time_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    uint8_t buf[7] = {0};
    esp_err_t err = _read_block(REG_SECONDS, buf, sizeof(buf));
    if (err != ESP_OK) return err;

    out->valid   = (buf[0] & VL_FLAG_MASK) == 0;
    out->second  = _bcd_to_bin(buf[0] & 0x7F);
    out->minute  = _bcd_to_bin(buf[1] & 0x7F);
    out->hour    = _bcd_to_bin(buf[2] & 0x3F);
    out->day     = _bcd_to_bin(buf[3] & 0x3F);
    out->weekday = (uint8_t)(buf[4] & 0x07);
    out->month   = _bcd_to_bin(buf[5] & 0x1F);
    out->year    = (uint16_t)(2000 + _bcd_to_bin(buf[6]));
    return ESP_OK;
}

esp_err_t pm_rtc_pcf8563_write(const pm_rtc_time_t* in) {
    if (!in) return ESP_ERR_INVALID_ARG;
    uint8_t buf[7];
    buf[0] = _bin_to_bcd(in->second) & 0x7F;   // VL cleared by write
    buf[1] = _bin_to_bcd(in->minute) & 0x7F;
    buf[2] = _bin_to_bcd(in->hour)   & 0x3F;
    buf[3] = _bin_to_bcd(in->day)    & 0x3F;
    buf[4] = in->weekday & 0x07;
    buf[5] = _bin_to_bcd(in->month)  & 0x1F;   // century=0 (20xx)
    int yr = (int)in->year - 2000;
    if (yr < 0) yr = 0;
    if (yr > 99) yr = 99;
    buf[6] = _bin_to_bcd((uint8_t)yr);
    return _write_block(REG_SECONDS, buf, sizeof(buf));
}

time_t pm_rtc_pcf8563_to_unix(const pm_rtc_time_t* in) {
    if (!in) return 0;
    struct tm tmv = {
        .tm_year = (int)in->year - 1900,
        .tm_mon  = (int)in->month - 1,
        .tm_mday = (int)in->day,
        .tm_hour = (int)in->hour,
        .tm_min  = (int)in->minute,
        .tm_sec  = (int)in->second,
    };
    // Treat the RTC as UTC. mktime() applies the local TZ; use
    // timegm if available, else manually adjust. ESP-IDF newlib
    // ships timegm under that name.
    return timegm(&tmv);
}

void pm_rtc_pcf8563_from_unix(time_t t, pm_rtc_time_t* out) {
    if (!out) return;
    struct tm tmv = {0};
    gmtime_r(&t, &tmv);
    out->year    = (uint16_t)(tmv.tm_year + 1900);
    out->month   = (uint8_t)(tmv.tm_mon + 1);
    out->day     = (uint8_t)tmv.tm_mday;
    out->weekday = (uint8_t)tmv.tm_wday;
    out->hour    = (uint8_t)tmv.tm_hour;
    out->minute  = (uint8_t)tmv.tm_min;
    out->second  = (uint8_t)tmv.tm_sec;
    out->valid   = true;
}

esp_err_t pm_rtc_pcf8563_sync_from_system(void) {
    time_t now = time(NULL);
    pm_rtc_time_t t;
    pm_rtc_pcf8563_from_unix(now, &t);
    return pm_rtc_pcf8563_write(&t);
}

esp_err_t pm_rtc_pcf8563_sync_to_system(void) {
    pm_rtc_time_t t;
    esp_err_t err = pm_rtc_pcf8563_read(&t);
    if (err != ESP_OK) return err;
    if (!t.valid) {
        ESP_LOGW(TAG, "RTC reports lost time — leaving system clock alone");
        return ESP_ERR_INVALID_STATE;
    }
    time_t unix_now = pm_rtc_pcf8563_to_unix(&t);
    struct timeval tv = { .tv_sec = unix_now, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "settimeofday failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "system time set from RTC: %04u-%02u-%02u %02u:%02u:%02u UTC",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    return ESP_OK;
}
