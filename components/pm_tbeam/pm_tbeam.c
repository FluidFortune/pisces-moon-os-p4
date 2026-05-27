// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_tbeam.c — Skeleton implementation
//
//  Phase 15 lands the protocol header and the P4-side bridge
//  task. The matching T-Beam firmware ("Pisces Moon Peer for
//  T-Beam S3") is a separate sub-project, deferred to Phase 16
//  pending the user's bring-up of the T-Beam itself.
//
//  Current behavior:
//    - pm_tbeam_init_auto() configures UART2 on IO25/IO27 at
//      921600 baud, spawns the receiver task, sends a ping,
//      and waits up to PM_TBEAM_PROBE_MS for a tbeam_ready
//      event in response. If received, registers the peer in
//      the registry. If not, releases the UART driver and
//      returns false (T-Beam not present — silent fallback).
//    - The receiver task line-buffers UART input, parses JSON
//      via cJSON, and dispatches to event handlers (forwarding
//      wifi_seen / ble_seen / lora_rx to the peer registry).
//    - pm_tbeam_send_cmd() writes JSON + newline to UART2.
//
//  The probe is forgiving. If the T-Beam isn't powered, the
//  pins are floating, or the user hasn't wired it yet, the
//  init returns false in 3 seconds and the OS proceeds. No
//  apps break. No errors logged at WARN level (the absence of
//  a modular peer is not an error).
// ============================================================

#include "pm_tbeam.h"
#include "pm_peer.h"
#include "pm_hal.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_TBEAM";

#define PM_TBEAM_PROBE_MS  3000
#define LINE_BUF_SIZE      512

static bool        s_connected   = false;
static bool        s_uart_open   = false;
static SemaphoreHandle_t s_ready_signal = NULL;
static TaskHandle_t      s_rx_task     = NULL;
static uint32_t    s_last_heartbeat_ms = 0;

// ── Capability list registered with pm_peer when T-Beam connects.
//    Note: gps_fix is INTENTIONALLY ABSENT — even though the
//    physical board has a GPS, we leave the BN-180 on the P4
//    side as the canonical fix source. Two GPSes adds power
//    cost with no real benefit.
static const char* const TBEAM_CAPS[] = {
    "wifi_scan",
    "wifi_capture",
    "ble_scan",
    "ble_gatt",
    "lora_tx",
    "lora_rx",
    "lora_mesh",
    NULL,
};

// ─────────────────────────────────────────────
//  Event handlers
// ─────────────────────────────────────────────
static void _handle_ready(cJSON* root) {
    const cJSON* fw = cJSON_GetObjectItem(root, "fw_version");
    pm_log_i(TAG, "T-Beam announced ready (fw=%s)",
             cJSON_IsString(fw) ? fw->valuestring : "unknown");
    s_connected = true;
    if (s_ready_signal) xSemaphoreGive(s_ready_signal);

    // Register in the peer registry — apps can now find us.
    pm_peer_announce(PM_PEER_KIND_TBEAM_S3,
                     "T-Beam Supreme S3",
                     TBEAM_CAPS);
}

static void _handle_status(cJSON* root) {
    (void)root;
    s_last_heartbeat_ms = pm_millis();
}

// Phase 15: events are logged but not yet routed to a global
// event bus — that's Phase 16 work (subscriber pattern in
// pm_peer). For now, secondary_scan app pulls T-Beam events
// directly via its own UART subscription if needed.
static void _handle_wifi_seen(cJSON* root) {
    (void)root;
    pm_log_d(TAG, "T-Beam wifi_seen event");
}

static void _handle_ble_seen(cJSON* root) {
    (void)root;
    pm_log_d(TAG, "T-Beam ble_seen event");
}

static void _handle_lora_rx(cJSON* root) {
    (void)root;
    pm_log_d(TAG, "T-Beam lora_rx event");
}

static void _handle_line(const char* line) {
    cJSON* root = cJSON_Parse(line);
    if (!root) {
        pm_log_d(TAG, "non-JSON line ignored");
        return;
    }
    const cJSON* ev = cJSON_GetObjectItem(root, "event");
    if (cJSON_IsString(ev)) {
        if      (!strcmp(ev->valuestring, "tbeam_ready"))     _handle_ready(root);
        else if (!strcmp(ev->valuestring, "tbeam_status"))    _handle_status(root);
        else if (!strcmp(ev->valuestring, "tbeam_wifi_seen")) _handle_wifi_seen(root);
        else if (!strcmp(ev->valuestring, "tbeam_ble_seen"))  _handle_ble_seen(root);
        else if (!strcmp(ev->valuestring, "tbeam_lora_rx"))   _handle_lora_rx(root);
        else pm_log_d(TAG, "unhandled tbeam event '%s'", ev->valuestring);
    }
    cJSON_Delete(root);
}

// ─────────────────────────────────────────────
//  Receiver task
// ─────────────────────────────────────────────
static void _rx_task(void* arg) {
    (void)arg;
    static char    line[LINE_BUF_SIZE];
    static int     linelen = 0;
    static uint8_t buf[256];

    while (s_uart_open) {
        int n = uart_read_bytes(PM_TBEAM_UART_NUM, buf, sizeof(buf),
                                 pdMS_TO_TICKS(100));
        if (n <= 0) {
            // Heartbeat check
            if (s_connected && s_last_heartbeat_ms != 0) {
                uint32_t now = pm_millis();
                if (now - s_last_heartbeat_ms > 3 * PM_TBEAM_HEARTBEAT_MS) {
                    pm_log_w(TAG, "T-Beam heartbeat lost; marking offline");
                    s_connected = false;
                    pm_peer_withdraw(PM_PEER_KIND_TBEAM_S3);
                }
            }
            continue;
        }
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (linelen > 0) {
                    line[linelen] = 0;
                    _handle_line(line);
                    linelen = 0;
                }
            } else if (linelen < LINE_BUF_SIZE - 1) {
                line[linelen++] = c;
            } else {
                pm_log_w(TAG, "line too long, resetting buffer");
                linelen = 0;
            }
        }
    }
    s_rx_task = NULL;
    vTaskDelete(NULL);
}

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────
bool pm_tbeam_init_auto(void) {
    if (s_uart_open) return s_connected;

    uart_config_t cfg = {
        .baud_rate = PM_TBEAM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    if (uart_driver_install(PM_TBEAM_UART_NUM, 2048, 0, 0, NULL, 0) != ESP_OK) {
        pm_log_w(TAG, "uart_driver_install failed");
        return false;
    }
    if (uart_param_config(PM_TBEAM_UART_NUM, &cfg) != ESP_OK ||
        uart_set_pin(PM_TBEAM_UART_NUM, PM_TBEAM_PIN_TX, PM_TBEAM_PIN_RX,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        pm_log_w(TAG, "uart configure failed");
        uart_driver_delete(PM_TBEAM_UART_NUM);
        return false;
    }
    s_uart_open = true;

    s_ready_signal = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(_rx_task, "pm_tbeam", 4096, NULL, 5, &s_rx_task, 0);

    // Probe: send ping, wait for tbeam_ready
    pm_log_i(TAG, "Probing for T-Beam Supreme S3 on UART2 (IO25/IO27)...");
    pm_tbeam_send_cmd("{\"cmd\":\"ping\"}");
    if (s_ready_signal &&
        xSemaphoreTake(s_ready_signal, pdMS_TO_TICKS(PM_TBEAM_PROBE_MS)) == pdTRUE)
    {
        pm_log_i(TAG, "→ T-Beam connected");
        s_last_heartbeat_ms = pm_millis();
        return true;
    }

    pm_log_i(TAG, "→ T-Beam not present (no response within %dms)",
              PM_TBEAM_PROBE_MS);
    pm_log_i(TAG, "→ Shutting down T-Beam subsystem (UART2 freed)");
    // T-Beam is a removable peripheral. If absent at boot, stop
    // polling and free UART2. Hot-plug re-detection is Phase 17.
    s_uart_open = false;                    // signals _rx_task to exit
    // Give the rx task a moment to notice s_uart_open and self-delete
    vTaskDelay(pdMS_TO_TICKS(150));
    uart_driver_delete(PM_TBEAM_UART_NUM);
    return false;
}

void pm_tbeam_send_cmd(const char* json_line) {
    if (!s_uart_open || !json_line) return;
    size_t n = strlen(json_line);
    uart_write_bytes(PM_TBEAM_UART_NUM, json_line, n);
    if (n == 0 || json_line[n - 1] != '\n') {
        uart_write_bytes(PM_TBEAM_UART_NUM, "\n", 1);
    }
}

bool pm_tbeam_connected(void) { return s_connected; }

int pm_tbeam_do(pm_tbeam_op_t op, const char* args) {
    char buf[256];
    const char* cmd = NULL;
    switch (op) {
        case PM_TBEAM_OP_WIFI_SCAN:           cmd = "wifi_scan"; break;
        case PM_TBEAM_OP_WIFI_PROMISC_START:  cmd = "wifi_promisc_start"; break;
        case PM_TBEAM_OP_WIFI_PROMISC_STOP:   cmd = "wifi_promisc_stop"; break;
        case PM_TBEAM_OP_BLE_SCAN_START:      cmd = "ble_scan_start"; break;
        case PM_TBEAM_OP_BLE_SCAN_STOP:       cmd = "ble_scan_stop"; break;
        case PM_TBEAM_OP_LORA_TX:             cmd = "lora_tx"; break;
        case PM_TBEAM_OP_LORA_RX_ARM:         cmd = "lora_rx_arm"; break;
        case PM_TBEAM_OP_PING:                cmd = "ping"; break;
    }
    if (!cmd) return -1;
    if (args && *args) {
        snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\",%s}", cmd, args);
    } else {
        snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}", cmd);
    }
    pm_tbeam_send_cmd(buf);
    return 0;
}
