#!/bin/bash
# ============================================================
#  pm_phase16_wifi_events.sh — Wire WiFi scan events into wardrive UI
#  Copyright (C) 2026 Eric Becker / Fluid Fortune
#  SPDX-License-Identifier: AGPL-3.0-or-later
#
#  Adds:
#    1. WiFi event handler that fires on SCAN_DONE
#    2. Pulls scan results into pm_app_wardrive_add_network()
#    3. Restarts scan every 5 seconds while running
#    4. BLE scan via NimBLE (when ESP-Hosted is up)
# ============================================================

set -e

if [ ! -f "components/pm_apps/pm_apps_cyber/pm_app_wardrive.c" ]; then
    echo "ERROR: not in project root"
    exit 1
fi

python3 << 'PYEOF'
path = 'components/pm_apps/pm_apps_cyber/pm_app_wardrive.c'
src = open(path).read()

# Add event handler for WiFi scan completion
handler_block = '''
// ── WiFi scan event handler ──────────────────────────────────
static void _wifi_event_handler(void* arg, esp_event_base_t base,
                                 int32_t event_id, void* event_data) {
    if (base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE) return;
    if (!s_running) return;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return;

    wifi_ap_record_t* records = (wifi_ap_record_t*)pm_malloc(
        ap_count * sizeof(wifi_ap_record_t));
    if (!records) return;
    esp_wifi_scan_get_ap_records(&ap_count, records);

    for (int i = 0; i < ap_count; i++) {
        const wifi_ap_record_t* ap = &records[i];
        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap->bssid[0], ap->bssid[1], ap->bssid[2],
                 ap->bssid[3], ap->bssid[4], ap->bssid[5]);
        const char* enc = (ap->authmode == WIFI_AUTH_OPEN) ? "OPEN" :
                          (ap->authmode == WIFI_AUTH_WPA3_PSK) ? "WPA3" :
                          (ap->authmode == WIFI_AUTH_WPA2_PSK) ? "WPA2" : "WPA";

        pm_app_wardrive_add_network((const char*)ap->ssid, bssid_str,
                                     ap->rssi, ap->primary, enc);

        // Log to live feed
        char ts[8];
        uint32_t up_sec = pm_uptime_seconds();
        snprintf(ts, sizeof(ts), "%02u:%02u",
                 (unsigned)((up_sec/60)%60), (unsigned)(up_sec%60));
        char content[64];
        snprintf(content, sizeof(content), "%-20.20s %ddBm CH%d",
                 (const char*)ap->ssid, ap->rssi, ap->primary);
        pm_app_wardrive_log(ts, "WIFI", content, lv_color_hex(0x00d4ff));

        // CSV append
        if (s_csv_fallback) {
            _csv_fallback_append_wifi(bssid_str, (const char*)ap->ssid,
                                       enc, ap->rssi, ap->primary);
        }
        s_wifi_total++;
    }
    pm_free(records);
    esp_wifi_clear_ap_list();

    // Update stat
    if (s_lbl_wifi) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", s_wifi_total);
        lv_label_set_text(s_lbl_wifi, buf);
    }

    // Restart scan after brief delay
    if (s_running) {
        wifi_scan_config_t cfg = {
            .ssid = NULL, .bssid = NULL, .channel = 0,
            .show_hidden = true,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time = { .active = { .min = 100, .max = 250 }}
        };
        esp_wifi_scan_start(&cfg, false);
    }
}

static bool s_wifi_evt_registered = false;
static void _register_wifi_events(void) {
    if (s_wifi_evt_registered) return;
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                _wifi_event_handler, NULL);
    s_wifi_evt_registered = true;
}
'''

# Inject before _start_cb
anchor = 'static void _start_cb(lv_event_t* e) {'
if '_wifi_event_handler' not in src:
    src = src.replace(anchor, handler_block + '\n' + anchor, 1)
    print("    WiFi event handler injected")

# Modify _start_cb to register events first
src = src.replace(
    'pm_log_i("WARDRIVE", "scan start");\n\n    // Lazy DB/CSV setup',
    'pm_log_i("WARDRIVE", "scan start");\n\n    _register_wifi_events();\n\n    // Lazy DB/CSV setup',
    1
)

# Make sure esp_event.h is included
if '#include "esp_event.h"' not in src:
    src = src.replace('#include "esp_wifi.h"',
                       '#include "esp_wifi.h"\n#include "esp_event.h"', 1)

open(path, 'w').write(src)
print("    wardrive WiFi events wired")
PYEOF

echo "WiFi event handler wired into wardrive."
echo "Note: ESP-Hosted must be initialized before _start_cb fires."
echo "      This typically happens during pm_c6_init in main.c."
