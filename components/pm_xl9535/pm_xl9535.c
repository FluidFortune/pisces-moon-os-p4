// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md

#include "pm_xl9535.h"

#include "pm_board.h"
#include "pm_bsp.h"

#if PM_BOARD_HAS_XL9535

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <string.h>

static const char* TAG = "PM_XL9535";

#define XL9535_REG_INPUT0      0x00
#define XL9535_REG_INPUT1      0x01
#define XL9535_REG_OUTPUT0     0x02
#define XL9535_REG_OUTPUT1     0x03
#define XL9535_REG_CONFIG0     0x06
#define XL9535_REG_CONFIG1     0x07

#define XL9535_ADDR            PM_BOARD_XL9535_I2C_ADDR

// Shadow output state — kept in sync with the chip so we can
// flip individual bits without round-tripping a read first.
static uint8_t s_p0 = 0;
static uint8_t s_p1 = 0;
static bool    s_initialised = false;

static esp_err_t _write_reg(uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { reg, val };
    return pm_bsp_i2c_transmit(XL9535_ADDR, tx, sizeof(tx), 50);
}

static esp_err_t _read_reg(uint8_t reg, uint8_t* out) {
    return pm_bsp_i2c_transmit_receive(XL9535_ADDR, &reg, 1, out, 1, 50);
}

static esp_err_t _flush_port0(void) { return _write_reg(XL9535_REG_OUTPUT0, s_p0); }
static esp_err_t _flush_port1(void) { return _write_reg(XL9535_REG_OUTPUT1, s_p1); }

// Translate the LilyGO IO numbering (0..7 = Port 0, 10..17 = Port 1)
// into a port + bit index.
static bool _split_pin(int pin, uint8_t* port, uint8_t* bit) {
    if (pin >= 0 && pin <= 7)   { *port = 0; *bit = (uint8_t)pin;       return true; }
    if (pin >= 10 && pin <= 17) { *port = 1; *bit = (uint8_t)(pin - 10); return true; }
    return false;
}

esp_err_t pm_xl9535_init(void) {
    if (s_initialised) return ESP_OK;

    // Configure both ports as outputs (0 = output on this part).
    esp_err_t err = _write_reg(XL9535_REG_CONFIG0, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config0 write failed: %s", esp_err_to_name(err));
        return err;
    }
    err = _write_reg(XL9535_REG_CONFIG1, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config1 write failed: %s", esp_err_to_name(err));
        return err;
    }

    // Safe default: everything off. Boot sequence will bring rails up.
    s_p0 = 0x00;
    s_p1 = 0x00;
    err = _flush_port0();
    if (err != ESP_OK) return err;
    err = _flush_port1();
    if (err != ESP_OK) return err;

    s_initialised = true;
    ESP_LOGI(TAG, "XL9535 initialised — all peripherals off");
    return ESP_OK;
}

esp_err_t pm_xl9535_set(int pin, bool on) {
    if (!s_initialised) {
        ESP_LOGE(TAG, "set(%d) before init", pin);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t port, bit;
    if (!_split_pin(pin, &port, &bit)) {
        ESP_LOGE(TAG, "invalid pin %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t* shadow = port ? &s_p1 : &s_p0;
    uint8_t mask = (uint8_t)(1u << bit);
    if (on) *shadow |= mask;
    else    *shadow &= (uint8_t)~mask;
    return port ? _flush_port1() : _flush_port0();
}

esp_err_t pm_xl9535_get(int pin, bool* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    uint8_t port, bit;
    if (!_split_pin(pin, &port, &bit)) return ESP_ERR_INVALID_ARG;
    uint8_t reg = port ? XL9535_REG_INPUT1 : XL9535_REG_INPUT0;
    uint8_t v = 0;
    esp_err_t err = _read_reg(reg, &v);
    if (err != ESP_OK) return err;
    *out = (v & (1u << bit)) != 0;
    return ESP_OK;
}

esp_err_t pm_xl9535_pulse_reset(int pin, uint32_t low_ms) {
    esp_err_t err = pm_xl9535_set(pin, false);
    if (err != ESP_OK) return err;
    if (low_ms == 0) low_ms = 10;
    vTaskDelay(pdMS_TO_TICKS(low_ms));
    return pm_xl9535_set(pin, true);
}

// ── Boot sequence ───────────────────────────────────────────
//
// Ordering mirrors the T-LoraPager XL9555 boot sequence in
// spi_treaty.h, adapted to the T-Display-P4's pin assignments.
// The constraints:
//   - 3.3V analog rail must come up before any peripheral
//   - SD must be up before pm_hal filesystem mounts /sd
//   - C6 enable must come up before ESP-Hosted is initialised
//   - Screen reset must be released before MIPI-DSI init runs
//   - LoRa reset must be released before pm_lora starts up
//
// Settle delays are generous on the first attempt — the C6
// in particular needs a clean power ramp before its firmware
// starts.
esp_err_t pm_xl9535_boot_sequence(void) {
    if (!s_initialised) {
        ESP_LOGE(TAG, "boot before init");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "XL9535 boot: bringing rails up");

    // 1. Main 3.3V analog rail — required for everything else.
    pm_xl9535_set(PM_XL9535_3V3_PWR_EN, true);
    vTaskDelay(pdMS_TO_TICKS(20));
    pm_xl9535_set(PM_XL9535_VCCA_PWR_EN, true);
    vTaskDelay(pdMS_TO_TICKS(20));
    pm_xl9535_set(PM_XL9535_5V0_PWR_EN, true);
    vTaskDelay(pdMS_TO_TICKS(20));

    // 2. SD enable — pm_hal will mount once we're done here.
    pm_xl9535_set(PM_XL9535_SD_EN, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "  SD power ON");

    // 3. ESP32-C6 enable + wake. The C6 firmware will start
    //    when the EN line goes high; the wake line is a level
    //    signal we keep asserted so the C6 doesn't drop into
    //    deep sleep on its own.
    pm_xl9535_set(PM_XL9535_C6_EN, true);
    pm_xl9535_set(PM_XL9535_C6_WAKE_UP, true);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "  ESP32-C6 EN+WAKE asserted");

    // 4. Screen reset released (pulse low briefly first to
    //    guarantee a clean startup if the panel was warm).
    pm_xl9535_set(PM_XL9535_SCREEN_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    pm_xl9535_set(PM_XL9535_SCREEN_RST, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "  Screen RST released");

    // 5. Touch controller reset — same pattern as screen.
    pm_xl9535_set(PM_XL9535_TOUCH_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    pm_xl9535_set(PM_XL9535_TOUCH_RST, true);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "  Touch RST released");

    // 6. SX1262 LoRa reset — pm_lora will initialise the chip
    //    after this point. SKY13453 antenna-switch control line
    //    starts low; pm_lora flips it per TX/RX state.
    pm_xl9535_set(PM_XL9535_SX1262_RST, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    pm_xl9535_set(PM_XL9535_SX1262_RST, true);
    pm_xl9535_set(PM_XL9535_SKY13453_VCTL, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "  SX1262 RST released");

    // 7. GPS wake (held high; the L76K has its own internal RTC
    //    so it remembers its almanac through brief power glitches).
    pm_xl9535_set(PM_XL9535_GPS_WAKE_UP, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "  GPS WAKE asserted");

    // 8. Ethernet PHY reset released (if anybody calls the
    //    Ethernet driver later, the PHY is ready).
    pm_xl9535_set(PM_XL9535_ETHERNET_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    pm_xl9535_set(PM_XL9535_ETHERNET_RST, true);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "  Ethernet PHY RST released");

    ESP_LOGI(TAG, "XL9535 boot sequence complete");
    return ESP_OK;
}

#else  // PM_BOARD_HAS_XL9535 == 0

// Stub for boards without an XL9535 — link-time silent no-ops.
esp_err_t pm_xl9535_init(void)          { return ESP_OK; }
esp_err_t pm_xl9535_boot_sequence(void) { return ESP_OK; }
esp_err_t pm_xl9535_set(int p, bool o)  { (void)p; (void)o; return ESP_OK; }
esp_err_t pm_xl9535_get(int p, bool* o) { (void)p; if (o) *o = false; return ESP_OK; }
esp_err_t pm_xl9535_pulse_reset(int p, uint32_t ms) { (void)p; (void)ms; return ESP_OK; }

#endif
