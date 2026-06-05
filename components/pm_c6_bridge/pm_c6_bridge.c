// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_c6_bridge.c — COMPATIBILITY SHIM
//
//  The original implementation of this file was a UART2 JSON
//  bridge to a custom firmware running on the Pisces Moon C6
//  ("Ghost Engine"). That firmware is abandoned. On the
//  Pisces Moon P4 the C6 ships with Espressif's standard
//  ESP-Hosted slave firmware and is reached transparently
//  over SDIO. The host calls esp_wifi_* / esp_ble_gap_*
//  as if it had built-in radios.
//
//  This file remains compiled because dozens of places across
//  the codebase include "pm_c6_bridge.h" or reference
//  pm_c6_cmd_*() helpers. Rather than ripple-change every
//  call site, we keep the header API stable and forward each
//  cmd into pm_radio_host, which is the real implementation.
//
//  Behaviour summary:
//
//    pm_c6_bridge_init(cbs)   — accepts the callbacks struct
//                                for API compatibility but
//                                ignores it. The real WiFi
//                                and BLE event fan-out lives
//                                in main.c (_on_wifi, _on_ble,
//                                _bt_gap_cb). Logs a one-time
//                                "compat shim" notice.
//    pm_c6_bridge_task(arg)   — sleeps forever. The UART
//                                receiver task is gone; ESP-
//                                Hosted handles transport.
//    pm_c6_cmd_*_start/stop   — forward to the matching
//                                pm_radio_host_* call.
//    pm_c6_cmd_send_raw       — no-op; logs once that custom
//                                JSON commands no longer have
//                                a destination.
//    pm_c6_cmd_ping / status  — no-op.
//
//  State flags (pm_c6_connected, pm_c6_wardrive_active,
//  pm_c6_ble_active) are still declared because legacy code
//  reads them. pm_c6_ble_active is updated by main.c's GAP
//  callback so apps that check it for "BLE is scanning"
//  continue to get the right answer.
// ============================================================

#include "pm_c6_bridge.h"
#include "pm_radio_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

static const char* TAG = "PM_C6_BRIDGE";

// State flags — declared in the header as `extern volatile`.
volatile bool pm_c6_connected        = false;  // legacy flag; no longer meaningful
volatile bool pm_c6_wardrive_active  = false;
volatile bool pm_c6_ble_active       = false;

static bool s_warned_send_raw = false;

void pm_c6_bridge_init(const pm_c6_callbacks_t* cbs) {
    (void)cbs;
    ESP_LOGW(TAG,
        "pm_c6_bridge is a compatibility shim; "
        "WiFi/BLE now run via ESP-Hosted on the host. "
        "Callbacks ignored; main.c owns event fan-out.");
}

void pm_c6_bridge_task(void* pvArgs) {
    (void)pvArgs;
    ESP_LOGW(TAG, "pm_c6_bridge_task started against a shim; sleeping forever");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}

// ── Forwarded cmds ─────────────────────────────────────────
void pm_c6_cmd_wardrive_start(void) {
    pm_radio_host_wifi_scan_subscribe();
    pm_c6_wardrive_active = true;
}

void pm_c6_cmd_wardrive_stop(void) {
    pm_radio_host_wifi_scan_unsubscribe();
    pm_c6_wardrive_active = false;
}

void pm_c6_cmd_ble_start(void) {
    pm_radio_host_ble_scan_start();
    pm_c6_ble_active = pm_radio_host_ble_scan_active();
}

void pm_c6_cmd_ble_stop(void) {
    pm_radio_host_ble_scan_stop();
    pm_c6_ble_active = false;
}

void pm_c6_cmd_promiscuous_start(void) {
    pm_radio_host_wifi_promisc_start();
}

void pm_c6_cmd_promiscuous_stop(void) {
    pm_radio_host_wifi_promisc_stop();
}

// raw_log was a custom Ghost Engine feature with no host-side
// equivalent. Map it to promiscuous since that's the closest
// behaviour (raw 802.11 frames). If the slave doesn't support
// promiscuous mode the underlying call will fail gracefully.
void pm_c6_cmd_raw_log_start(void) { pm_radio_host_wifi_promisc_start(); }
void pm_c6_cmd_raw_log_stop(void)  { pm_radio_host_wifi_promisc_stop();  }

// Ping and status had meaning only when there was a custom
// firmware to query. Now they're harmless logs so callers
// don't crash.
void pm_c6_cmd_ping(void)   { ESP_LOGD(TAG, "ping (shim)"); }
void pm_c6_cmd_status(void) { ESP_LOGD(TAG, "status (shim)"); }

void pm_c6_cmd_send_raw(const char* json_line) {
    (void)json_line;
    if (!s_warned_send_raw) {
        s_warned_send_raw = true;
        ESP_LOGW(TAG,
            "pm_c6_cmd_send_raw is no longer a transport. "
            "Use pm_radio_host_* or esp_*  APIs directly.");
    }
}
