// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_lora.cpp — RadioLib SX1262 wrapper
//
//  RadioLib is a C++ library; this file is the only C++ unit
//  in the P4 firmware. The wrapper exposes a clean C API.
//
//  RadioLib's EspHal provides the SPI plumbing for ESP-IDF;
//  we reuse it to keep this file short and avoid duplicating
//  bus management. EspHal is NOT however SPI-Treaty-aware,
//  so the public API takes/gives the Pisces SPI mutex around
//  every radio call.
//
//  Build dep (idf_component.yml main):
//    jgromes/radiolib: ^7.2.1
//
//  Mode parameters — chosen to match Phase 7 stubs:
//    VOICE:  2-FSK, 100 kbps, 50 kHz dev, 250 kHz BW, sync 0x12
//    MESH:   LoRa SF11 BW250 CR4/8 LongFast sync 0x2B
//
//  Phase-12 TODO before wireless go-live:
//    - Verify the four pins (NSS/DIO1/RST/BUSY) against the
//      ELECROW Eagle schematic — defaults in pm_lora.h are
//      placeholders.
//    - Confirm whether the SX1262 carrier uses TCXO or XTAL
//      (TCXO needs setTCXO() at init). Most Ai-Thinker boards
//      use XTAL; some EBYTE use TCXO.
//    - Confirm DIO2 routes to RF switch (most modules do); if
//      not, separate DIO control lines must be wired.
// ============================================================

#include "pm_lora.h"
#include "pm_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <RadioLib.h>
#include <EspHal.h>
#include <string.h>

extern "C" {

static const char* TAG = "PM_LORA";

// ── Static state ────────────────────────────────────────────
static bool             s_inited        = false;
static pm_lora_mode_t   s_mode          = PM_LORA_MODE_MESH;
static SemaphoreHandle_t s_handle_mtx   = NULL;
static const char*       s_holder       = "(none)";
static int               s_last_rssi    = 0;
static float             s_last_snr     = 0.0f;

static pm_lora_rx_cb_t   s_rx_cb        = nullptr;
static void*             s_rx_user      = nullptr;
static volatile bool     s_rx_irq       = false;
static TaskHandle_t      s_rx_task      = nullptr;

// RadioLib radio object — only one is live at a time. We
// switch between FSK and LoRa configurations on the same chip.
static EspHal*           s_hal          = nullptr;
static SX1262*           s_radio        = nullptr;

// ── DIO1 ISR ────────────────────────────────────────────────
// RadioLib's setPacketReceivedAction takes a void(*)() — it's
// invoked from the SX1262 IRQ on DIO1. We just flag the worker.
static void IRAM_ATTR _on_dio1(void) {
    s_rx_irq = true;
    BaseType_t hp = pdFALSE;
    if (s_rx_task) vTaskNotifyGiveFromISR(s_rx_task, &hp);
    portYIELD_FROM_ISR(hp);
}

// ── RX worker ───────────────────────────────────────────────
//  Wakes on DIO1 IRQ, reads packet, invokes user callback.
//  Re-enters RX afterwards so the next packet flows.
static void _rx_task_fn(void* arg) {
    (void)arg;
    uint8_t buf[256];
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_rx_irq) continue;
        s_rx_irq = false;

        if (!s_radio || !s_rx_cb) continue;

        size_t plen = 0;
        int    state = RADIOLIB_ERR_SPI_CMD_FAILED;
        PM_SPI_TAKE("lora_rx") {
            plen = s_radio->getPacketLength();
            if (plen > sizeof(buf)) plen = sizeof(buf);
            state = s_radio->readData(buf, plen);
            s_last_rssi = (int)s_radio->getRSSI();
            s_last_snr  = s_radio->getSNR();
            // re-arm
            s_radio->startReceive();
        } PM_SPI_GIVE();

        if (state == RADIOLIB_ERR_NONE) {
            s_rx_cb(buf, plen, s_last_rssi, s_last_snr, s_rx_user);
        } else {
            ESP_LOGW(TAG, "rx readData err=%d", state);
        }
    }
}

// ── Mode application helpers ────────────────────────────────
static int _apply_voice(void) {
    int rc = s_radio->beginFSK(906.5f, 100.0f, 50.0f, 234.3f,
                                10, 16);  // freq, br, dev, BW, pwr, preamble
    if (rc != RADIOLIB_ERR_NONE) return rc;
    s_radio->setSyncWord(0x12);
    s_radio->setEncoding(RADIOLIB_ENCODING_NRZ);
    s_radio->fixedPacketLengthMode(44);   // Codec2 frame size
    return RADIOLIB_ERR_NONE;
}

static int _apply_mesh(void) {
    // Meshtastic LongFast (US default 906.875 MHz).
    int rc = s_radio->begin(906.875f, 250.0f, 11, 8, 0x2B, 22, 16, 1.6f);
    return rc;     // (freq, BW, SF, CR, sync, pwr_dBm, preamble, TCXO_volt)
}

// ── Public API ──────────────────────────────────────────────
pm_lora_status_t pm_lora_init(void) {
    if (s_inited) return PM_LORA_OK;

    // Phase 13: If pm_radio has already initialized us via the
    // backend bridge, our state should already say s_inited=true
    // and we'd have returned above. If not, do a fresh init —
    // this path is hit when an app calls pm_lora_init() before
    // pm_radio_init_auto() has run, e.g. in a unit-test scenario
    // or if auto-detect was skipped.

    s_handle_mtx = xSemaphoreCreateMutex();
    if (!s_handle_mtx) return PM_LORA_ERR;

    // EspHal: (sck, miso, mosi). Bus is shared via PM_SPI_TAKE.
    extern int pm_hal_spi_sck_pin (void);
    extern int pm_hal_spi_miso_pin(void);
    extern int pm_hal_spi_mosi_pin(void);
    s_hal   = new EspHal(pm_hal_spi_sck_pin(),
                          pm_hal_spi_miso_pin(),
                          pm_hal_spi_mosi_pin());
    s_radio = new SX1262(new Module(s_hal,
                                       PM_LORA_PIN_NSS,
                                       PM_LORA_PIN_DIO1,
                                       PM_LORA_PIN_RST,
                                       PM_LORA_PIN_BUSY));

    int rc = RADIOLIB_ERR_SPI_CMD_FAILED;
    PM_SPI_TAKE("lora_init") {
        rc = _apply_mesh();    // Default to mesh on boot
    } PM_SPI_GIVE();

    if (rc != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "begin() failed: %d", rc);
        return PM_LORA_ERR;
    }
    s_mode = PM_LORA_MODE_MESH;

    // RX worker
    xTaskCreatePinnedToCore(_rx_task_fn, "pm_lora_rx", 4096, NULL, 6, &s_rx_task, 0);
    s_radio->setPacketReceivedAction(_on_dio1);

    s_inited = true;
    ESP_LOGI(TAG, "SX1262 initialized at 906.875 MHz LongFast (mesh default)");
    return PM_LORA_OK;
}

bool pm_lora_is_initialized(void) { return s_inited; }

pm_lora_status_t pm_lora_set_mode_voice(void) {
    if (!s_inited) return PM_LORA_NOT_INIT;
    int rc = RADIOLIB_ERR_SPI_CMD_FAILED;
    PM_SPI_TAKE("lora_mode_v") { rc = _apply_voice(); } PM_SPI_GIVE();
    if (rc != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "voice mode failed: %d", rc);
        return PM_LORA_ERR;
    }
    s_mode = PM_LORA_MODE_VOICE;
    return PM_LORA_OK;
}

pm_lora_status_t pm_lora_set_mode_mesh(void) {
    if (!s_inited) return PM_LORA_NOT_INIT;
    int rc = RADIOLIB_ERR_SPI_CMD_FAILED;
    PM_SPI_TAKE("lora_mode_m") { rc = _apply_mesh(); } PM_SPI_GIVE();
    if (rc != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "mesh mode failed: %d", rc);
        return PM_LORA_ERR;
    }
    s_mode = PM_LORA_MODE_MESH;
    return PM_LORA_OK;
}

pm_lora_mode_t pm_lora_current_mode(void) { return s_mode; }

// ── Treaty ──────────────────────────────────────────────────
bool pm_lora_take(uint32_t timeout_ms, const char* who) {
    if (!s_handle_mtx) return false;
    TickType_t t = (timeout_ms == 0xFFFFFFFFu) ? portMAX_DELAY
                                                : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_handle_mtx, t) == pdTRUE) {
        s_holder = who ? who : "(unknown)";
        return true;
    }
    return false;
}

void pm_lora_give(void) {
    if (!s_handle_mtx) return;
    s_holder = "(none)";
    xSemaphoreGive(s_handle_mtx);
}

// ── Frequency ───────────────────────────────────────────────
pm_lora_status_t pm_lora_set_freq_mhz(float mhz) {
    if (!s_inited) return PM_LORA_NOT_INIT;
    int rc = RADIOLIB_ERR_SPI_CMD_FAILED;
    PM_SPI_TAKE("lora_freq") { rc = s_radio->setFrequency(mhz); } PM_SPI_GIVE();
    return rc == RADIOLIB_ERR_NONE ? PM_LORA_OK : PM_LORA_ERR;
}

// ── TX / RX ─────────────────────────────────────────────────
pm_lora_status_t pm_lora_tx(const uint8_t* buf, size_t len, uint32_t timeout_ms) {
    (void)timeout_ms;     // RadioLib transmit() blocks; honor it via SPI mutex.
    if (!s_inited)         return PM_LORA_NOT_INIT;
    if (!buf || len == 0)  return PM_LORA_ERR;

    int rc = RADIOLIB_ERR_SPI_CMD_FAILED;
    PM_SPI_TAKE("lora_tx") {
        rc = s_radio->transmit(buf, len);
        if (s_rx_cb) s_radio->startReceive();   // resume RX after TX
    } PM_SPI_GIVE();

    if (rc != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "tx err=%d", rc);
        return PM_LORA_TX_FAIL;
    }
    return PM_LORA_OK;
}

pm_lora_status_t pm_lora_set_rx_cb(pm_lora_rx_cb_t cb, void* user) {
    if (!s_inited) return PM_LORA_NOT_INIT;
    s_rx_cb   = cb;
    s_rx_user = user;
    if (cb) {
        int rc = RADIOLIB_ERR_NONE;
        PM_SPI_TAKE("lora_rx_arm") { rc = s_radio->startReceive(); } PM_SPI_GIVE();
        return rc == RADIOLIB_ERR_NONE ? PM_LORA_OK : PM_LORA_ERR;
    } else {
        PM_SPI_TAKE("lora_rx_off") { s_radio->standby(); } PM_SPI_GIVE();
        return PM_LORA_OK;
    }
}

int   pm_lora_last_rssi(void) { return s_last_rssi; }
float pm_lora_last_snr (void) { return s_last_snr;  }

}  // extern "C"

// ────────────────────────────────────────────────────────────────
//  pm_radio backend bridge (Phase 13 — radio abstraction layer)
//
//  pm_radio.c calls into these C symbols to drive the SX1262 as
//  one of several possible backends. We just delegate to the
//  existing pm_lora_* API, which already handles the SPI Treaty
//  correctly. Translation between pm_lora_status_t and
//  pm_radio_status_t is shape-equivalent.
// ────────────────────────────────────────────────────────────────
extern "C" {

#include "pm_radio.h"

bool pm_lora_backend_init(void) {
    return pm_lora_init() == PM_LORA_OK;
}

pm_radio_status_t pm_lora_backend_tx(const uint8_t* b, size_t n, uint32_t to) {
    pm_lora_status_t r = pm_lora_tx(b, n, to);
    if (r == PM_LORA_OK)       return PM_RADIO_OK;
    if (r == PM_LORA_NOT_INIT) return PM_RADIO_NOT_INIT;
    if (r == PM_LORA_BUSY)     return PM_RADIO_BUSY;
    if (r == PM_LORA_TX_FAIL)  return PM_RADIO_TX_FAIL;
    return PM_RADIO_ERR;
}

// pm_lora's RX callback signature matches pm_radio's exactly,
// so we pass it through unchanged.
pm_radio_status_t pm_lora_backend_set_rx_cb(pm_radio_rx_cb_t cb, void* user) {
    pm_lora_status_t r = pm_lora_set_rx_cb((pm_lora_rx_cb_t)cb, user);
    return r == PM_LORA_OK ? PM_RADIO_OK : PM_RADIO_ERR;
}

void pm_lora_backend_info(pm_radio_info_t* out) {
    if (!out) return;
    out->last_rssi = pm_lora_last_rssi();
    out->last_snr  = pm_lora_last_snr();
    out->mode_str  = (pm_lora_current_mode() == PM_LORA_MODE_VOICE)
                       ? "FSK voice 100kbps"
                       : "LongFast SF11 BW250";
    // tx/rx counters lived in the apps; not tracked here yet.
}

}  // extern "C"
