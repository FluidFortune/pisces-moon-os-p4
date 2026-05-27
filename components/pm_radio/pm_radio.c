// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_radio.c — Auto-detect and dispatch
//
//  Detection strategy:
//
//  Both SX1262 and nRF24L01+ are SPI peripherals on the same
//  bus, but their command formats are completely different. We
//  can probe each by issuing its known idle/status command and
//  checking for a chip-specific response signature.
//
//  We probe in this order (most likely first):
//    1. SX1262: pulse NRST, send GET_STATUS (0xC0), check that
//       status byte has the expected mode bits (CHIPMODE field
//       in 0x70..0x60 = STBY_RC after reset).
//    2. nRF24: read STATUS reg (R_REGISTER | 0x07). Reset value
//       is 0x0E. Then write CONFIG (0x20) = 0x0B and read back
//       to confirm writes stick.
//
//  False-positive mitigation:
//    - SX1262 GET_STATUS on a missing chip returns 0x00 or 0xFF
//      (MISO floats). We require a status byte with at least
//      one bit set in the mode field AND at least one bit clear.
//    - nRF24 probe writes a known pattern and reads it back. If
//      writes don't stick (no chip), the probe fails.
//    - If the SX1262 probe succeeds, we don't run the nRF24 probe
//      at all (avoids confusing one chip with the other's probe).
//
//  After a successful detection, pm_radio_init_auto() calls into
//  the backend's setup. Backend keeps the SPI handle for the
//  duration of the session.
// ============================================================

#include "pm_radio.h"
#include "pm_hal.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "PM_RADIO";

// ── Backend forward decls ───────────────────────────────────
extern bool              pm_lora_backend_init    (void);
extern pm_radio_status_t pm_lora_backend_tx      (const uint8_t* b, size_t n, uint32_t to);
extern pm_radio_status_t pm_lora_backend_set_rx_cb(pm_radio_rx_cb_t cb, void* u);
extern void              pm_lora_backend_info    (pm_radio_info_t* out);

extern bool              pm_nrf24_backend_init   (void);
extern pm_radio_status_t pm_nrf24_backend_tx     (const uint8_t* b, size_t n, uint32_t to);
extern pm_radio_status_t pm_nrf24_backend_set_rx_cb(pm_radio_rx_cb_t cb, void* u);
extern void              pm_nrf24_backend_info   (pm_radio_info_t* out);

// ── State ───────────────────────────────────────────────────
static pm_radio_kind_t   s_kind        = PM_RADIO_NONE;
static SemaphoreHandle_t s_handle_mtx  = NULL;
static const char*       s_holder      = "(none)";
static spi_device_handle_t s_probe_dev = NULL;
static bool             s_spi_bus_owned = false;

// ─────────────────────────────────────────────
//  SPI helper for probes (separate device handle from the
//  backend's; we release it after detection completes).
//
//  Probes own the SPI2 bus only for the duration of detection.
//  Once we hand off to a backend (pm_lora / pm_nrf24), the
//  backend's RadioLib EspHal driver takes the bus over. We
//  release our handle before backend init so EspHal can claim
//  the bus cleanly.
//
//  Note: if a backend has ALREADY initialized the SPI bus on
//  some earlier path, spi_bus_initialize returns ESP_ERR_INVALID_STATE,
//  which we treat as "bus is already up, fine to add devices."
// ─────────────────────────────────────────────
static esp_err_t _probe_spi_bus_init(void) {
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PM_RADIO_PIN_SPI_MOSI,
        .miso_io_num     = PM_RADIO_PIN_SPI_MISO,
        .sclk_io_num     = PM_RADIO_PIN_SPI_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        s_spi_bus_owned = true;
        return ESP_OK;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        // Bus already initialized by someone else (probably fine)
        s_spi_bus_owned = false;
        return ESP_OK;
    }
    return err;
}

static void _probe_spi_bus_done(void) {
    if (s_spi_bus_owned) {
        spi_bus_free(SPI2_HOST);
        s_spi_bus_owned = false;
    }
}

static esp_err_t _probe_spi_init(int cs_pin) {
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = 1 * 1000 * 1000,    // 1 MHz: slow + safe
        .mode           = 0,
        .spics_io_num   = cs_pin,
        .queue_size     = 1,
        .flags          = 0,
    };
    return spi_bus_add_device(SPI2_HOST, &cfg, &s_probe_dev);
}

static void _probe_spi_done(void) {
    if (s_probe_dev) {
        spi_bus_remove_device(s_probe_dev);
        s_probe_dev = NULL;
    }
}

// ─────────────────────────────────────────────
//  SX1262 probe
//
//  Pulse NRST low for 1ms, release, wait 10ms (chip enters
//  STBY_RC). Send GET_STATUS opcode. Expected response:
//    bits[6:4] = CHIPMODE = 010 (STBY_RC) post-reset
//    bits[3:1] = COMMAND_STATUS = various, but not all-zero
//
//  We don't require an exact value — just that the response
//  isn't 0x00, 0xFF, or another all-same byte (which would
//  indicate a floating MISO line, i.e. no chip).
// ─────────────────────────────────────────────
static bool _probe_sx1262(void) {
    // Pulse NRST
    gpio_set_direction(PM_RADIO_PIN_CTL_D, GPIO_MODE_OUTPUT);
    gpio_set_level(PM_RADIO_PIN_CTL_D, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(PM_RADIO_PIN_CTL_D, 1);
    vTaskDelay(pdMS_TO_TICKS(15));

    // GET_STATUS = 0xC0; SX1262 returns one status byte on the
    // following clock cycles. We send 0xC0 then a NOP (0x00)
    // and read the response of the second byte.
    uint8_t txbuf[2] = { 0xC0, 0x00 };
    uint8_t rxbuf[2] = { 0, 0 };

    if (_probe_spi_init(PM_RADIO_PIN_CTL_A) != ESP_OK) return false;

    spi_transaction_t t = {
        .length    = 16,           // 2 bytes
        .tx_buffer = txbuf,
        .rx_buffer = rxbuf,
    };
    esp_err_t err = spi_device_polling_transmit(s_probe_dev, &t);
    _probe_spi_done();
    if (err != ESP_OK) return false;

    uint8_t status = rxbuf[1];
    // Reject obvious "no chip" patterns
    if (status == 0x00 || status == 0xFF) return false;
    // Status byte must have at least one bit set in the chipmode
    // field (bits 6:4) — anything other than 000 (unused).
    uint8_t chipmode = (status >> 4) & 0x07;
    if (chipmode == 0) return false;

    ESP_LOGI(TAG, "SX1262 probe: status=0x%02X chipmode=%d ✓", status, chipmode);
    return true;
}

// ─────────────────────────────────────────────
//  nRF24L01+ probe
//
//  Read STATUS register (cmd byte = R_REGISTER | 0x07 = 0x07).
//  Reset value is 0x0E. Then write the CONFIG register (0x20)
//  with 0x0B (PWR_UP=1, CRC=1, PRIM_RX=1) and read it back.
//  If the read-back matches what we wrote, a real chip is here.
//
//  Note: nRF24 has no NRST pin; it uses internal POR. We simply
//  probe directly. Since the SX1262 NRST pin (=54) shares wiring
//  with nRF24's CE (=53 — different pin), pulsing NRST during
//  the SX1262 probe doesn't disturb the nRF24.
// ─────────────────────────────────────────────
static bool _probe_nrf24(void) {
    if (_probe_spi_init(PM_RADIO_PIN_CTL_A) != ESP_OK) return false;

    // STATUS read
    uint8_t tx_status[1] = { 0xFF };          // 0xFF = NOP returns STATUS
    uint8_t rx_status[1] = { 0 };
    spi_transaction_t t1 = {
        .length    = 8,
        .tx_buffer = tx_status,
        .rx_buffer = rx_status,
    };
    if (spi_device_polling_transmit(s_probe_dev, &t1) != ESP_OK) {
        _probe_spi_done();
        return false;
    }
    uint8_t status = rx_status[0];

    // Write CONFIG = 0x0B, then read back
    uint8_t tx_w[2] = { 0x20, 0x0B };
    spi_transaction_t t2 = { .length = 16, .tx_buffer = tx_w };
    if (spi_device_polling_transmit(s_probe_dev, &t2) != ESP_OK) {
        _probe_spi_done(); return false;
    }
    uint8_t tx_r[2] = { 0x00, 0x00 };          // R_REGISTER | 0x00 = 0x00
    uint8_t rx_r[2] = { 0, 0 };
    spi_transaction_t t3 = {
        .length    = 16,
        .tx_buffer = tx_r,
        .rx_buffer = rx_r,
    };
    if (spi_device_polling_transmit(s_probe_dev, &t3) != ESP_OK) {
        _probe_spi_done(); return false;
    }
    _probe_spi_done();

    // rx_r[0] = STATUS again, rx_r[1] = CONFIG
    if (rx_r[1] != 0x0B) {
        ESP_LOGD(TAG, "nRF24 probe: write didn't stick (config=0x%02X)", rx_r[1]);
        return false;
    }
    // status should also have at least one bit set; full-zero is suspicious
    if (status == 0xFF) {
        ESP_LOGD(TAG, "nRF24 probe: STATUS=0xFF (likely floating MISO)");
        return false;
    }

    ESP_LOGI(TAG, "nRF24 probe: STATUS=0x%02X CONFIG=0x%02X ✓", status, rx_r[1]);
    return true;
}

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────
const char* pm_radio_name(pm_radio_kind_t k) {
    switch (k) {
        case PM_RADIO_NONE:     return "none";
        case PM_RADIO_SX1262:   return "SX1262";
        case PM_RADIO_NRF24:    return "nRF24L01+";
        case PM_RADIO_ESP32_H2: return "ESP32-H2";
        case PM_RADIO_ESP32_C6: return "ESP32-C6 (slot)";
        case PM_RADIO_HALOW:    return "Wi-Fi HaLow";
        default:                  return "unknown";
    }
}

pm_radio_kind_t pm_radio_kind(void) { return s_kind; }

pm_radio_kind_t pm_radio_init_auto(void) {
    if (!s_handle_mtx) s_handle_mtx = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "Probing wireless module slot…");

    // Bring up the SPI bus for probing. Probes own it; once we hand
    // off to a backend we release it so the backend (RadioLib EspHal)
    // can manage the bus itself.
    if (_probe_spi_bus_init() != ESP_OK) {
        ESP_LOGW(TAG, "SPI bus init for probe failed — assuming no radio");
        s_kind = PM_RADIO_NONE;
        return s_kind;
    }

    bool sx_present = _probe_sx1262();
    bool nrf_present = false;
    if (!sx_present) nrf_present = _probe_nrf24();

    _probe_spi_bus_done();    // release bus before backend init

    if (sx_present) {
        if (pm_lora_backend_init()) {
            s_kind = PM_RADIO_SX1262;
            ESP_LOGI(TAG, "→ %s ready", pm_radio_name(s_kind));
            return s_kind;
        }
        ESP_LOGW(TAG, "SX1262 detected but backend init failed");
    }

    if (nrf_present) {
        if (pm_nrf24_backend_init()) {
            s_kind = PM_RADIO_NRF24;
            ESP_LOGI(TAG, "→ %s ready", pm_radio_name(s_kind));
            return s_kind;
        }
        ESP_LOGW(TAG, "nRF24 detected but backend init failed");
    }

    s_kind = PM_RADIO_NONE;
    ESP_LOGI(TAG, "→ no module detected (slot empty)");
    return s_kind;
}

esp_err_t pm_radio_init_as(pm_radio_kind_t kind) {
    if (!s_handle_mtx) s_handle_mtx = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "Forced init: %s", pm_radio_name(kind));
    bool ok = false;
    switch (kind) {
        case PM_RADIO_SX1262: ok = pm_lora_backend_init();  break;
        case PM_RADIO_NRF24:  ok = pm_nrf24_backend_init(); break;
        case PM_RADIO_ESP32_H2:
        case PM_RADIO_ESP32_C6:
        case PM_RADIO_HALOW:
            ESP_LOGW(TAG, "%s backend not yet implemented",
                     pm_radio_name(kind));
            return ESP_ERR_NOT_SUPPORTED;
        default: return ESP_ERR_INVALID_ARG;
    }
    if (!ok) return ESP_FAIL;
    s_kind = kind;
    return ESP_OK;
}

// ── Treaty ──────────────────────────────────────────────────
bool pm_radio_take(uint32_t timeout_ms, const char* who) {
    if (!s_handle_mtx) return false;
    TickType_t t = (timeout_ms == 0xFFFFFFFFu) ? portMAX_DELAY
                                                : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_handle_mtx, t) == pdTRUE) {
        s_holder = who ? who : "(unknown)";
        return true;
    }
    return false;
}

void pm_radio_give(void) {
    if (!s_handle_mtx) return;
    s_holder = "(none)";
    xSemaphoreGive(s_handle_mtx);
}

// ── TX/RX dispatch ──────────────────────────────────────────
pm_radio_status_t pm_radio_tx(const uint8_t* buf, size_t len,
                                uint32_t timeout_ms) {
    switch (s_kind) {
        case PM_RADIO_SX1262: return pm_lora_backend_tx(buf, len, timeout_ms);
        case PM_RADIO_NRF24:  return pm_nrf24_backend_tx(buf, len, timeout_ms);
        case PM_RADIO_NONE:    return PM_RADIO_NOT_INIT;
        default:                return PM_RADIO_NOT_IMPL;
    }
}

pm_radio_status_t pm_radio_set_rx_cb(pm_radio_rx_cb_t cb, void* user) {
    switch (s_kind) {
        case PM_RADIO_SX1262: return pm_lora_backend_set_rx_cb(cb, user);
        case PM_RADIO_NRF24:  return pm_nrf24_backend_set_rx_cb(cb, user);
        case PM_RADIO_NONE:    return PM_RADIO_NOT_INIT;
        default:                return PM_RADIO_NOT_IMPL;
    }
}

void pm_radio_info(pm_radio_info_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->kind = s_kind;
    out->initialized = (s_kind != PM_RADIO_NONE);
    switch (s_kind) {
        case PM_RADIO_SX1262: pm_lora_backend_info(out);  break;
        case PM_RADIO_NRF24:  pm_nrf24_backend_info(out); break;
        default: break;
    }
}
