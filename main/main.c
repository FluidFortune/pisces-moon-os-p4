// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  main.c — Pisces Moon P4 boot entry
//
//  Boot order:
//    1. NVS init (required by WiFi/BLE if anything on P4 ever
//       talks to those, even though normally that's the C6).
//    2. pm_hal_init() — log, time, PSRAM heap, SPI Treaty mutex,
//       SD card mount.
//    3. BSP init — display + touch + audio (LVGL backbone).
//       TODO: integrate Elecrow components/bsp_extra.
//    4. pm_launcher_init() — build LVGL screens.
//    5. main_register_apps() — every app calls pm_app_register().
//    6. pm_c6_bridge_init() + spawn receiver task.
//    7. Show launcher.
//    8. Main loop = LVGL handler + per-app tick().
//
//  Note: app-side WiFi/BLE init is NOT done here. Radios
//  live on the C6. The P4 receives JSON events.
// ============================================================

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "pm_hal.h"
#include "pm_app.h"
#include "pm_launcher.h"
#include "pm_c6_bridge.h"
#include "pm_sqlite.h"
#include "pm_bsp.h"
#include "pm_gps_uart.h"
#include "pm_radio.h"
#include "esp_err.h"

static const char* TAG = "PM_MAIN";

#include "pm_gps_state.h"
#include "pm_app_wifi.h"
#include "pm_app_bluetooth.h"
#include "pm_app_wardrive.h"
#include "pm_app_bt_radar.h"
#include "pm_app_beacon.h"
#include "pm_app_pkt_sniffer.h"
#include "pm_app_probe_intel.h"

// ─────────────────────────────────────────────
//  C6 event handlers
//  Single source: bridge → fan-out to:
//    - shared state caches (pm_gps_state)
//    - app-side ring buffers (pm_app_wifi, pm_app_bluetooth)
//    - CYBER session loggers (pm_app_wardrive, pm_app_bt_radar,
//      pm_app_beacon, pm_app_pkt_sniffer, pm_app_probe_intel)
// ─────────────────────────────────────────────
static void _on_wifi(const char* mac, const char* ssid, int rssi,
                     int ch, const char* enc, double lat, double lng) {
    pm_log_d(TAG, "[WIFI] %s '%s' rssi=%d ch=%d enc=%s",
             mac, ssid, rssi, ch, enc);
    pm_app_wifi_on_scan_result(ssid, mac, rssi, ch, enc);
    pm_app_beacon_on_wifi(mac, ssid, rssi, ch, enc);
    pm_app_wardrive_on_wifi(mac, ssid, rssi, ch, enc);
    (void)lat; (void)lng;
}

static void _on_ble(const char* mac, const char* name, int rssi,
                    double lat, double lng,
                    const char* addr_type, const char* mfg) {
    pm_log_d(TAG, "[BLE] %s '%s' rssi=%d type=%s",
             mac, name, rssi, addr_type);
    pm_app_bluetooth_on_seen(mac, name, rssi, addr_type, mfg);
    pm_app_bt_radar_on_seen(mac, name, rssi, addr_type, mfg);
    pm_app_wardrive_on_ble(mac, name, rssi, addr_type, mfg);
    (void)lat; (void)lng;
}

static void _on_probe(const char* mac, const char* ssid, int rssi,
                      int count, double lat, double lng) {
    pm_log_d(TAG, "[PROBE] %s -> '%s' rssi=%d cnt=%d",
             mac, ssid, rssi, count);
    pm_app_probe_intel_on_probe(mac, ssid, rssi, count);
    pm_app_wardrive_on_probe(mac, ssid, rssi, count);
    (void)lat; (void)lng;
}

static void _on_gps(double lat, double lng, double alt,
                    int sats, bool valid) {
    pm_log_d(TAG, "[GPS] %.6f,%.6f alt=%.1fm sats=%d valid=%d",
             lat, lng, alt, sats, valid);
    pm_gps_state_set(lat, lng, alt, sats, valid, 0.0);
}

static void _on_pkt(const char* frame_type, const char* src, int rssi) {
    pm_log_d(TAG, "[PKT] %s from %s rssi=%d", frame_type, src, rssi);
    pm_app_pkt_sniffer_on_pkt(frame_type, src, rssi);
    pm_app_wardrive_on_pkt(frame_type, src, rssi);
}

static void _on_ready(const char* firmware, const char* version) {
    pm_log_i(TAG, "C6 Ghost Engine online: %s v%s", firmware, version);
}

static void _on_status(bool wifi, bool ble, int nets, int devs, uint32_t up) {
    pm_log_i(TAG, "C6 status: wifi=%d ble=%d nets=%d devs=%d up=%us",
             wifi, ble, nets, devs, (unsigned)up);
}

// ─────────────────────────────────────────────
//  Forward decl — implemented in pm_apps_register.c
//  (or wherever each app's *_register() function calls
//   pm_app_register).
// ─────────────────────────────────────────────
extern void main_register_apps(void);

// Weak default so we can boot before all apps are written.
__attribute__((weak)) void main_register_apps(void) {
    pm_log_w(TAG, "main_register_apps() not yet linked — running with empty registry");
}

// ─────────────────────────────────────────────
//  Main UI loop — runs the LVGL handler + app ticks
// ─────────────────────────────────────────────
static void _ui_loop_task(void* arg) {
    (void)arg;
    pm_log_i(TAG, "UI loop starting");

    uint32_t last = pm_millis();

    while (1) {
        // TODO_LVGL: lv_timer_handler();

        // Per-app tick
        const pm_app_t* cur = pm_app_current();
        if (cur && cur->tick) {
            uint32_t now = pm_millis();
            cur->tick(now - last);
            last = now;
        } else {
            last = pm_millis();
        }

        // ~60fps cadence — adjust once display refresh is wired up
        pm_delay_ms(16);
    }
}

// ─────────────────────────────────────────────
//  app_main — ESP-IDF entry point
// ─────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "=== Pisces Moon OS — Pisces Moon P4 v%s ===", PM_VERSION_STRING);
    ESP_LOGI(TAG, "Boot.");

    // 1. NVS
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. HAL
    pm_hal_init();

    // 2b. SQLite — used by wardrive (CYBER) for session DB.
    //     Falls back to per-session CSV if init fails.
    pm_sqlite_global_init();

    // 3. BSP — MIPI-DSI display, GT911 touch, LVGL plumbing,
    //          backlight PWM, I2C bus. After this returns OK,
    //          screens are flushable and touch events flow.
    if (pm_bsp_init() != ESP_OK) {
        pm_log_e(TAG, "BSP init failed — running headless");
        // We continue anyway: bridge + radio paths still work
        // for diagnostics, even with no UI.
    } else {
        pm_bsp_start_lvgl_tick_task();
    }

    // 3b. GPS — P4-direct NMEA parser on IO2/IO3. Failure is
    //          non-fatal; pm_gps_state stays "no fix" if the
    //          module isn't connected or sends nothing.
    if (pm_gps_uart_init() != ESP_OK) {
        pm_log_w(TAG, "GPS UART init failed — continuing without GPS");
    }

    // 3c. Wireless module slot — auto-detect what's plugged in.
    //     Returns NONE cleanly if the slot is empty; mesh/voice/
    //     nRF24 apps handle that case in their UI. SX1262 and
    //     nRF24 are auto-detected via SPI signature probes; H2,
    //     C6 (slot variant), and HaLow are user-declared since
    //     they're full MCUs needing their own firmware.
    pm_radio_kind_t radio = pm_radio_init_auto();
    pm_log_i(TAG, "Wireless slot: %s", pm_radio_name(radio));

    // 4. Launcher
    pm_launcher_init();

    // 5. App registry
    main_register_apps();
    pm_log_i(TAG, "%d apps registered", pm_app_count());

    // 6. C6 bridge
    pm_c6_callbacks_t cbs = {
        .on_wifi   = _on_wifi,
        .on_ble    = _on_ble,
        .on_probe  = _on_probe,
        .on_gps    = _on_gps,
        .on_pkt    = _on_pkt,
        .on_ready  = _on_ready,
        .on_status = _on_status,
    };
    pm_c6_bridge_init(&cbs);

    xTaskCreatePinnedToCore(
        pm_c6_bridge_task,
        "c6_bridge",
        4096,
        NULL,
        5,
        NULL,
        0     // Core 0 — radio events drained off-core from UI
    );

    // Send a ping so we know if the C6 is alive at boot
    pm_delay_ms(200);
    pm_c6_cmd_ping();

    // 7. Show launcher
    pm_launcher_show();

    // 8. UI loop — pinned to Core 1 to keep LVGL away from radio drain
    xTaskCreatePinnedToCore(
        _ui_loop_task,
        "ui_loop",
        8192,
        NULL,
        4,
        NULL,
        1
    );

    pm_log_i(TAG, "Boot complete. Pisces Moon P4 is running.");
    // app_main returns; FreeRTOS scheduler keeps everything alive.
}
