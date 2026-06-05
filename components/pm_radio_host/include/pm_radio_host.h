// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_radio_host.h — Host-side WiFi/BLE control
//
//  The Pisces Moon P4 SoC has no built-in radios. WiFi and
//  Bluetooth are provided by the on-board ESP32-C6 running
//  Espressif's standard ESP-Hosted slave firmware, reached
//  over SDIO. From the P4's perspective the standard
//  esp_wifi_* / esp_ble_gap_* APIs work as if the radios were
//  local — ESP-Hosted forwards everything transparently.
//
//  This module is the thin "I want a scan" / "I want to stop"
//  layer that sits on top of those APIs.
//
//  ── WiFi scanning is reference-counted ──
//  Multiple apps can want continuous WiFi scan at the same
//  time (e.g. wardrive logger running in the background while
//  the user has the wifi app open). Apps subscribe on enter
//  and unsubscribe on exit; the radio keeps sweeping as long
//  as at least one subscriber is interested. When the last
//  subscriber leaves, the scan winds down.
//
//  ── BLE scanning is a single bool ──
//  ESP-Hosted's BLE GAP scan runs continuously once started.
//  Multiple callers can call start; only one stop turns it
//  off. The boot path starts it once and apps generally never
//  need to call these — BLE adverts just flow through the
//  GAP callback wired up in main.c.
//
//  ── Promiscuous WiFi ──
//  Best-effort. Returns ESP_ERR_NOT_SUPPORTED if the C6's
//  ESP-Hosted slave firmware wasn't built with promiscuous
//  forwarding. Apps that need this (pkt_sniffer, probe_intel,
//  wpa_hs) must handle that failure gracefully.
// ============================================================

#ifndef PM_RADIO_HOST_H
#define PM_RADIO_HOST_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called from main.c after ESP-Hosted brings WiFi/BLE up.
void pm_radio_host_mark_wifi_ready(bool ready);
void pm_radio_host_mark_ble_ready(bool ready);

bool pm_radio_host_wifi_ready(void);
bool pm_radio_host_ble_ready(void);

// ── WiFi scan, reference-counted ──
//
// subscribe() bumps the consumer count; if it was 0 the radio
// kicks a fresh scan immediately.
// unsubscribe() drops the count; on reaching 0 the scan winds
// down naturally (current sweep finishes; no re-kick).
//
// Calls are idempotent for any single caller — subscribe twice
// will leak a count, so apps must pair subscribe/unsubscribe
// 1:1 across enter/exit.
esp_err_t pm_radio_host_wifi_scan_subscribe(void);
esp_err_t pm_radio_host_wifi_scan_unsubscribe(void);
int       pm_radio_host_wifi_scan_subscribers(void);
bool      pm_radio_host_wifi_scan_active(void);

// Called by main.c from the WIFI_EVENT_SCAN_DONE handler. If
// subscribers are still interested, re-kicks scan. Otherwise
// clears the active flag.
void pm_radio_host_on_wifi_scan_done(void);

// ── BLE scan, simple bool ──
esp_err_t pm_radio_host_ble_scan_start(void);
esp_err_t pm_radio_host_ble_scan_stop(void);
bool      pm_radio_host_ble_scan_active(void);

// Called by main.c from the GAP scan_param_set_complete event
// handler when the underlying GAP scan actually starts.
void pm_radio_host_mark_ble_scanning(bool active);

// ── Promiscuous WiFi ──
esp_err_t pm_radio_host_wifi_promisc_start(void);
esp_err_t pm_radio_host_wifi_promisc_stop(void);
bool      pm_radio_host_wifi_promisc_active(void);

#ifdef __cplusplus
}
#endif

#endif // PM_RADIO_HOST_H
