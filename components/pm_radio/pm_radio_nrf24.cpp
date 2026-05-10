// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_radio_nrf24.cpp — nRF24L01+ backend
//
//  Built on RadioLib's nRF24 class — same library we use for
//  SX1262, just a different chip class. The public C ABI
//  (pm_nrf24_backend_*) is what pm_radio.c calls into.
//
//  nRF24 specifics:
//    - 2.4 GHz, channels 0..125 (each 1 MHz wide)
//    - Payload 1..32 bytes, optional Enhanced ShockBurst
//    - "Pipes" — up to 6 RX addresses on one chip
//    - No RSSI; only RPD (received power detector, 1-bit)
//
//  Default config: channel 76 (2476 MHz), 1Mbps, 5-byte addr
//  "PISCE", payload size variable. Enhanced ShockBurst on
//  with auto-ack and auto-retry.
//
//  Cyber-app potential: a separate "nrf24_sniffer" app could
//  drop ESB, set channel, and use promiscuous-style hopping
//  to capture nearby toy/peripheral traffic (Mousejack class).
//  That app isn't built here — this backend just gives apps
//  a clean radio handle.
//
//  Note: this file pulls RadioLib in C++. Since pm_radio.c is
//  C, the linkage is via extern "C" symbols.
// ============================================================

#include "pm_radio.h"
#include "pm_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <RadioLib.h>
#include <EspHal.h>
#include <string.h>

extern "C" {

static const char* TAG = "PM_NRF24";

static EspHal*           s_hal      = nullptr;
static nRF24*            s_radio    = nullptr;
static bool              s_inited   = false;
static int               s_last_rssi = 0;     // nRF24 has no real RSSI; we report RPD as -50/-90
static float             s_last_snr  = 0.0f;
static uint32_t          s_tx_count = 0;
static uint32_t          s_rx_count = 0;
static pm_radio_rx_cb_t  s_rx_cb    = nullptr;
static void*             s_rx_user  = nullptr;
static volatile bool     s_rx_irq   = false;
static TaskHandle_t      s_rx_task  = nullptr;

static const uint8_t s_addr[5] = { 'P', 'I', 'S', 'C', 'E' };

static void IRAM_ATTR _on_irq(void) {
    s_rx_irq = true;
    BaseType_t hp = pdFALSE;
    if (s_rx_task) vTaskNotifyGiveFromISR(s_rx_task, &hp);
    portYIELD_FROM_ISR(hp);
}

static void _rx_task_fn(void* arg) {
    (void)arg;
    uint8_t buf[32];
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!s_rx_irq) continue;
        s_rx_irq = false;
        if (!s_radio || !s_rx_cb) continue;

        size_t plen = 0;
        int    state;
        PM_SPI_TAKE("nrf24_rx") {
            plen  = s_radio->getPacketLength();
            if (plen > sizeof(buf)) plen = sizeof(buf);
            state = s_radio->readData(buf, plen);
            // RPD is 1-bit: high → strong signal, low → weak
            // Map to a ballpark dBm for UI consistency.
            s_last_rssi = -50;    // RadioLib doesn't expose RPD directly
            s_radio->startReceive();
        } PM_SPI_GIVE();

        if (state == RADIOLIB_ERR_NONE) {
            s_rx_count++;
            s_rx_cb(buf, plen, s_last_rssi, s_last_snr, s_rx_user);
        }
    }
}

bool pm_nrf24_backend_init(void) {
    if (s_inited) return true;
    extern int pm_hal_spi_sck_pin (void);
    extern int pm_hal_spi_miso_pin(void);
    extern int pm_hal_spi_mosi_pin(void);

    s_hal   = new EspHal(pm_hal_spi_sck_pin(),
                          pm_hal_spi_miso_pin(),
                          pm_hal_spi_mosi_pin());
    // RadioLib nRF24 ctor: Module(hal, cs, irq, ce)
    s_radio = new nRF24(new Module(s_hal,
                                       PM_RADIO_PIN_CTL_A,    // CS
                                       PM_RADIO_PIN_CTL_B,    // IRQ
                                       RADIOLIB_NC,
                                       PM_RADIO_PIN_CTL_C));  // CE

    int rc;
    PM_SPI_TAKE("nrf24_init") {
        // begin(freq_MHz, dataRate_kbps, power_dBm, address_len, payload_size)
        // Channel 76 = 2476 MHz; 1 Mbps; 0 dBm power.
        rc = s_radio->begin(2476.0f, 1000, 0, 5, 32);
        if (rc == RADIOLIB_ERR_NONE) {
            // Set both TX and RX address pipes to our PISCE prefix
            s_radio->setTransmitPipe(s_addr);
            s_radio->setReceivePipe(0, s_addr);
        }
    } PM_SPI_GIVE();

    if (rc != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "nRF24 begin failed: %d", rc);
        return false;
    }

    xTaskCreatePinnedToCore(_rx_task_fn, "pm_nrf24_rx", 4096, NULL, 6, &s_rx_task, 0);
    s_radio->setIrqAction(_on_irq);

    s_inited = true;
    ESP_LOGI(TAG, "nRF24 backend ready (ch76, 1Mbps, addr 'PISCE', 32B payload)");
    return true;
}

pm_radio_status_t pm_nrf24_backend_tx(const uint8_t* buf, size_t len,
                                        uint32_t to) {
    (void)to;
    if (!s_inited)         return PM_RADIO_NOT_INIT;
    if (!buf || len == 0)  return PM_RADIO_BAD_PARAM;
    if (len > 32)          return PM_RADIO_BAD_PARAM;

    int rc;
    PM_SPI_TAKE("nrf24_tx") {
        rc = s_radio->transmit(buf, len);
        if (s_rx_cb) s_radio->startReceive();
    } PM_SPI_GIVE();
    if (rc != RADIOLIB_ERR_NONE) return PM_RADIO_TX_FAIL;
    s_tx_count++;
    return PM_RADIO_OK;
}

pm_radio_status_t pm_nrf24_backend_set_rx_cb(pm_radio_rx_cb_t cb, void* user) {
    if (!s_inited) return PM_RADIO_NOT_INIT;
    s_rx_cb   = cb;
    s_rx_user = user;
    if (cb) {
        int rc;
        PM_SPI_TAKE("nrf24_rx_arm") { rc = s_radio->startReceive(); } PM_SPI_GIVE();
        return rc == RADIOLIB_ERR_NONE ? PM_RADIO_OK : PM_RADIO_ERR;
    } else {
        PM_SPI_TAKE("nrf24_rx_off") { s_radio->standby(); } PM_SPI_GIVE();
        return PM_RADIO_OK;
    }
}

void pm_nrf24_backend_info(pm_radio_info_t* out) {
    if (!out) return;
    out->last_rssi = s_last_rssi;
    out->last_snr  = s_last_snr;
    out->tx_count  = s_tx_count;
    out->rx_count  = s_rx_count;
    out->mode_str  = "ShockBurst ch76 1Mbps";
}

}  // extern "C"
