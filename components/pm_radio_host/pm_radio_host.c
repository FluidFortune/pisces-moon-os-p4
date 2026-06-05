// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_radio_host.c — Host-side WiFi/BLE control wrappers
//
//  Each domain (WiFi scan / BLE scan / WiFi promiscuous) keeps
//  a handful of flags. The start/stop functions check
//  readiness, mutate the flags, and call the underlying
//  esp_wifi or esp_ble_gap function. The interesting design
//  decision is WiFi scan's subscriber counter:
//
//    subscribers = 0     → no app wants scan; SCAN_DONE clears
//                          active and stops there
//    subscribers > 0     → at least one app is interested;
//                          SCAN_DONE re-kicks immediately
//
//  This lets background loggers (wardrive) co-exist with
//  foreground browsers (wifi app) without one stopping the
//  other when it exits.
//
//  Concurrency: subscriber count is touched by the LVGL tick
//  thread (subscribe/unsubscribe via app enter/exit) and by
//  the WiFi event task (on_wifi_scan_done). Reads/writes of
//  int are atomic on RISC-V; the worst tear is a redundant
//  re-kick on race, which the lower layer tolerates.
// ============================================================

#include "pm_radio_host.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#if CONFIG_BT_ENABLED && CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
#include "esp_gap_ble_api.h"
#endif

static const char* TAG = "PM_RADIO_HOST";

static bool s_wifi_ready          = false;
static bool s_ble_ready           = false;
static int  s_wifi_subscribers    = 0;
static bool s_wifi_scan_active    = false;
static bool s_ble_scan_active     = false;
static bool s_wifi_promisc_active = false;

// ── Ready flags ─────────────────────────────────────────────
void pm_radio_host_mark_wifi_ready(bool ready) {
    s_wifi_ready = ready;
    ESP_LOGI(TAG, "WiFi host marked %s", ready ? "READY" : "DOWN");
}

void pm_radio_host_mark_ble_ready(bool ready) {
    s_ble_ready = ready;
    ESP_LOGI(TAG, "BLE host marked %s", ready ? "READY" : "DOWN");
}

bool pm_radio_host_wifi_ready(void) { return s_wifi_ready; }
bool pm_radio_host_ble_ready(void)  { return s_ble_ready; }

// ── Internal WiFi scan kicker ──
//
// Passive sweep, hidden APs included. 120 ms per channel × 14
// channels is well under the 2 s budget that keeps the LVGL
// event loop responsive.
static esp_err_t _kick_wifi_scan(void) {
    wifi_scan_config_t cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time   = {
            .passive = 120,
        },
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err == ESP_OK) {
        s_wifi_scan_active = true;
    } else if (err == ESP_ERR_WIFI_STATE) {
        // Scan already running — that's fine, treat as success.
        s_wifi_scan_active = true;
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "esp_wifi_scan_start: %s", esp_err_to_name(err));
    }
    return err;
}

// ── WiFi scan subscription ──
esp_err_t pm_radio_host_wifi_scan_subscribe(void) {
    if (!s_wifi_ready) {
        ESP_LOGW(TAG, "wifi subscribe but WiFi not ready");
        return ESP_ERR_INVALID_STATE;
    }
    int was = s_wifi_subscribers++;
    if (was == 0) {
        ESP_LOGI(TAG, "first subscriber; kicking WiFi scan");
        return _kick_wifi_scan();
    }
    return ESP_OK;
}

esp_err_t pm_radio_host_wifi_scan_unsubscribe(void) {
    if (s_wifi_subscribers > 0) {
        s_wifi_subscribers--;
    }
    if (s_wifi_subscribers == 0) {
        ESP_LOGI(TAG, "last subscriber left; WiFi scan will wind down");
        // We don't actively stop here — the current sweep finishes
        // and SCAN_DONE doesn't re-kick. Active stops are racy with
        // the in-flight scan and produce log noise.
    }
    return ESP_OK;
}

int  pm_radio_host_wifi_scan_subscribers(void) { return s_wifi_subscribers; }
bool pm_radio_host_wifi_scan_active(void)      { return s_wifi_scan_active; }

void pm_radio_host_on_wifi_scan_done(void) {
    s_wifi_scan_active = false;
    if (s_wifi_subscribers > 0 && s_wifi_ready) {
        _kick_wifi_scan();
    }
}

// ── BLE scan ────────────────────────────────────────────────
esp_err_t pm_radio_host_ble_scan_start(void) {
#if CONFIG_BT_ENABLED && CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
    if (!s_ble_ready) {
        ESP_LOGW(TAG, "ble scan requested but BLE not ready");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ble_scan_active) return ESP_OK;
    esp_err_t err = esp_ble_gap_start_scanning(0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_ble_gap_start_scanning: %s", esp_err_to_name(err));
    }
    // s_ble_scan_active flips true when main.c's GAP callback gets
    // the SCAN_START_COMPLETE event and calls mark_ble_scanning.
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t pm_radio_host_ble_scan_stop(void) {
#if CONFIG_BT_ENABLED && CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
    if (!s_ble_scan_active) return ESP_OK;
    esp_err_t err = esp_ble_gap_stop_scanning();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_ble_gap_stop_scanning: %s", esp_err_to_name(err));
    }
    s_ble_scan_active = false;
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool pm_radio_host_ble_scan_active(void) { return s_ble_scan_active; }

void pm_radio_host_mark_ble_scanning(bool active) {
    s_ble_scan_active = active;
}

// ── Promiscuous WiFi ────────────────────────────────────────
esp_err_t pm_radio_host_wifi_promisc_start(void) {
    if (!s_wifi_ready) return ESP_ERR_INVALID_STATE;
    if (s_wifi_promisc_active) return ESP_OK;
    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err == ESP_OK) {
        s_wifi_promisc_active = true;
        ESP_LOGI(TAG, "promiscuous mode ON");
    } else {
        ESP_LOGW(TAG, "esp_wifi_set_promiscuous(true): %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t pm_radio_host_wifi_promisc_stop(void) {
    if (!s_wifi_promisc_active) return ESP_OK;
    esp_err_t err = esp_wifi_set_promiscuous(false);
    s_wifi_promisc_active = false;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_promiscuous(false): %s", esp_err_to_name(err));
    }
    return err;
}

bool pm_radio_host_wifi_promisc_active(void) { return s_wifi_promisc_active; }
