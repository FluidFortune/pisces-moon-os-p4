// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  main.c — Pisces Moon P4 boot entry
//
//  Boot order:
//    1. NVS init
//    2. pm_hal_init   — log, time, PSRAM, SPI Treaty, SD mount
//    2b. pm_sqlite_global_init — wardrive DB infrastructure
//    3. pm_bsp_init   — MIPI-DSI display, GT911 touch, LVGL plumbing
//    3b. GPS source selection — Cardputer ADV UART1 header by default
//    3c. pm_radio_init_auto — probe wireless slot, init SX1262/nRF24
//    4. pm_launcher_init — LVGL category and app screens
//    5. main_register_apps — every app calls pm_app_register
//    6. ESP-Hosted C6 WiFi/BLE stack
//    7. pm_launcher_show — first frame visible
//    8. UI loop task on Core 1 (per-app tick at 60Hz)
//
//  Note: WiFi/BLE station scan lives on the C6. GPS/LoRa/keyboard/
//  second-radio expansion can be supplied by a Cardputer ADV over
//  the UART1 header bridge.
// ============================================================

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_hosted.h"
#include "esp_lvgl_port.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#if CONFIG_BT_ENABLED && CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
#include "esp_bt_main.h"
#include "esp_bluedroid_hci.h"
#include "esp_gap_ble_api.h"
#include "esp_hosted_bluedroid.h"
#endif

#include "pm_hal.h"
#include "pm_app.h"
#include "pm_launcher.h"
#include "pm_c6_bridge.h"
#include "pm_sqlite.h"
#include "pm_bsp.h"
#include "pm_gps_uart.h"
#include "pm_radio.h"
#include "pm_peer.h"
#include "pm_tbeam.h"
#include "esp_err.h"
#include "pm_boot.h"
#include "pm_board.h"
#include "pm_cardputer_i2c.h"

static const char* TAG = "PM_MAIN";
static TaskHandle_t s_service_bringup_task = NULL;

#define PM_BOOT_STEP_PACE_MS       180
#define PM_BOOT_SECTION_PACE_MS    350
#define PM_BOOT_HANDOFF_PACE_MS    600
#define PM_BOOT_SPLASH_MS          2600

static void _boot_pace(uint32_t ms) {
    if (ms) vTaskDelay(pdMS_TO_TICKS(ms));
}

static void _boot_visual_probe(bool on) {
    (void)on;
    // DISABLED: was reconfiguring PM_PIN_LCD_BL as plain GPIO,
    // stealing it from LEDC. Caused periodic backlight pulses
    // under heavy render load. Boot UI now handled by pm_boot.
}

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

static esp_err_t _init_c6_hosted_wifi(void) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        pm_log_w(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        pm_log_w(TAG, "event loop init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = (esp_err_t)esp_hosted_init();
    if (err != ESP_OK) {
        pm_log_w(TAG, "esp_hosted_init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_t* sta = esp_netif_create_default_wifi_sta();
    if (!sta) {
        pm_log_w(TAG, "default WiFi STA netif create failed");
        return ESP_FAIL;
    }

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) {
        pm_log_w(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        pm_log_w(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        pm_log_w(TAG, "esp_wifi_set_mode(STA) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        pm_log_w(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    pm_log_i(TAG, "C6 ESP-Hosted WiFi STA ready");
    return ESP_OK;
}

#if CONFIG_BT_ENABLED && CONFIG_ESP_HOSTED_ENABLE_BT_BLUEDROID
static esp_ble_scan_params_t s_ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x80,
    .scan_window = 0x40,
    .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE,
};

static const char* _ble_addr_type_name(esp_ble_addr_type_t t) {
    switch (t) {
        case BLE_ADDR_TYPE_PUBLIC: return "public";
        case BLE_ADDR_TYPE_RANDOM: return "random";
        case BLE_ADDR_TYPE_RPA_PUBLIC: return "rpa_pub";
        case BLE_ADDR_TYPE_RPA_RANDOM: return "rpa_rnd";
        default: return "unknown";
    }
}

static void _hex_preview(const uint8_t* data, uint8_t len,
                         char* out, size_t out_len) {
    static const char k_hex[] = "0123456789ABCDEF";
    if (!out || out_len == 0) return;
    size_t pos = 0;
    uint8_t n = len > 8 ? 8 : len;
    for (uint8_t i = 0; data && i < n && pos + 2 < out_len; i++) {
        out[pos++] = k_hex[(data[i] >> 4) & 0x0f];
        out[pos++] = k_hex[data[i] & 0x0f];
    }
    out[pos] = 0;
}

static void _bt_gap_cb(esp_gap_ble_cb_event_t event,
                       esp_ble_gap_cb_param_t* param) {
    if (!param) return;

    switch (event) {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            if (param->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                esp_err_t err = esp_ble_gap_start_scanning(0);
                if (err != ESP_OK) {
                    pm_log_w(TAG, "BLE scan start failed: %s", esp_err_to_name(err));
                } else {
                    pm_log_i(TAG, "ESP-Hosted BLE scan active");
                    pm_c6_ble_active = true;
                }
            } else {
                pm_log_w(TAG, "BLE scan params failed: status=%d",
                         (int)param->scan_param_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;

            char mac[18];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                     param->scan_rst.bda[0], param->scan_rst.bda[1],
                     param->scan_rst.bda[2], param->scan_rst.bda[3],
                     param->scan_rst.bda[4], param->scan_rst.bda[5]);

            uint16_t adv_len = (uint16_t)param->scan_rst.adv_data_len +
                               (uint16_t)param->scan_rst.scan_rsp_len;
            uint8_t name_len = 0;
            uint8_t* name = esp_ble_resolve_adv_data_by_type(
                param->scan_rst.ble_adv, adv_len,
                ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
            if (!name || name_len == 0) {
                name = esp_ble_resolve_adv_data_by_type(
                    param->scan_rst.ble_adv, adv_len,
                    ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
            }

            char name_buf[32] = "";
            if (name && name_len > 0) {
                uint8_t n = name_len >= sizeof(name_buf) ? sizeof(name_buf) - 1 : name_len;
                memcpy(name_buf, name, n);
                name_buf[n] = 0;
            }

            uint8_t mfg_len = 0;
            uint8_t* mfg = esp_ble_resolve_adv_data_by_type(
                param->scan_rst.ble_adv, adv_len,
                ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mfg_len);
            char mfg_buf[24] = "";
            _hex_preview(mfg, mfg_len, mfg_buf, sizeof(mfg_buf));

            _on_ble(mac, name_buf, param->scan_rst.rssi, 0.0, 0.0,
                    _ble_addr_type_name(param->scan_rst.ble_addr_type),
                    mfg_buf);
            break;
        }

        default:
            break;
    }
}

static esp_err_t _init_c6_hosted_bluetooth(void) {
    esp_err_t err = esp_hosted_bt_controller_init();
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_SUPPORTED) {
            pm_log_w(TAG, "ESP-Hosted BT controller RPC unsupported; continuing host-only VHCI path");
        } else {
            pm_log_w(TAG, "esp_hosted_bt_controller_init failed: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        err = esp_hosted_bt_controller_enable();
        if (err != ESP_OK) {
            if (err == ESP_ERR_NOT_SUPPORTED) {
                pm_log_w(TAG, "ESP-Hosted BT enable RPC unsupported; continuing host-only VHCI path");
            } else {
                pm_log_w(TAG, "esp_hosted_bt_controller_enable failed: %s", esp_err_to_name(err));
                return err;
            }
        }
    }

    pm_log_i(TAG, "Opening ESP-Hosted Bluedroid VHCI path");
    hosted_hci_bluedroid_open();
    esp_bluedroid_hci_driver_operations_t ops = {
        .send = hosted_hci_bluedroid_send,
        .check_send_available = hosted_hci_bluedroid_check_send_available,
        .register_host_callback = hosted_hci_bluedroid_register_host_callback,
    };
    err = esp_bluedroid_attach_hci_driver(&ops);
    if (err != ESP_OK) {
        pm_log_w(TAG, "Bluedroid HCI attach failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_bluedroid_status_t st = esp_bluedroid_get_status();
    if (st == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        err = esp_bluedroid_init();
        if (err != ESP_OK) {
            pm_log_w(TAG, "esp_bluedroid_init failed: %s", esp_err_to_name(err));
            return err;
        }
        st = esp_bluedroid_get_status();
    }
    if (st == ESP_BLUEDROID_STATUS_INITIALIZED) {
        err = esp_bluedroid_enable();
        if (err != ESP_OK) {
            pm_log_w(TAG, "esp_bluedroid_enable failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    err = esp_ble_gap_register_callback(_bt_gap_cb);
    if (err != ESP_OK) {
        pm_log_w(TAG, "BLE GAP callback failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gap_set_scan_params(&s_ble_scan_params);
    if (err != ESP_OK) {
        pm_log_w(TAG, "BLE scan params submit failed: %s", esp_err_to_name(err));
        return err;
    }

    pm_log_i(TAG, "ESP-Hosted Bluedroid host ready");
    return ESP_OK;
}
#else
static esp_err_t _init_c6_hosted_bluetooth(void) {
    pm_log_w(TAG, "ESP-Hosted Bluetooth disabled in sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

// ─────────────────────────────────────────────
//  Forward decl — implemented in pm_apps_register.c
//  (or wherever each app's *_register() function calls
//   pm_app_register).
// ─────────────────────────────────────────────
extern void main_register_apps(void);

// ─────────────────────────────────────────────
//  Main UI loop — runs the LVGL handler + app ticks
// ─────────────────────────────────────────────
static void _ui_loop_task(void* arg) {
    (void)arg;
    pm_log_i(TAG, "UI loop starting");

    uint32_t last = pm_millis();

    while (1) {
        // LVGL's own timer handler runs on the esp_lvgl_port task
        // (pinned to Core 1 by pm_bsp_init). We don't need to call
        // lv_timer_handler() here.

        // Per-app tick
        const pm_app_t* cur = pm_app_current();
        if (cur && cur->tick) {
            uint32_t now = pm_millis();
            if (lvgl_port_lock(5)) {
                cur->tick(now - last);
                lvgl_port_unlock();
                last = now;
            } else {
                last = now;
            }
        } else {
            last = pm_millis();
        }

        // ~60Hz cadence for the per-app tick
        pm_delay_ms(16);
    }
}

static void _service_bringup_delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void _service_bringup_task(void* arg) {
    (void)arg;
    pm_log_i(TAG, "System service bring-up starting on OS core");

    _service_bringup_delay(100);
    pm_log_i(TAG, "Service: ESP-Hosted WiFi");
    esp_err_t hosted = _init_c6_hosted_wifi();
    pm_log_i(TAG, "ESP-Hosted WiFi: %s", esp_err_to_name(hosted));

    _service_bringup_delay(100);
    pm_log_i(TAG, "Service: SQLite init");
    if (!pm_sqlite_global_init()) {
        pm_log_w(TAG, "SQLite init failed; DB-backed apps will fall back");
    }

    _service_bringup_delay(250);
    pm_log_i(TAG, "Service: wireless slot probe");
    pm_radio_kind_t radio = pm_radio_init_auto();
    pm_log_i(TAG, "Wireless slot: %s", pm_radio_name(radio));

    _service_bringup_delay(250);
    pm_log_i(TAG, "Service: optional peer probes");
    pm_peer_probe_optional();

    _service_bringup_delay(250);
    pm_log_i(TAG, "Service: T-Beam probe");
    pm_tbeam_init_auto();

    _service_bringup_delay(100);
    bool external_ble_sensor =
        pm_cardputer_i2c_link_seen() || pm_tbeam_connected();
    if (external_ble_sensor) {
        pm_log_i(TAG, "Service: BLE source is external; local hosted BT left idle");
    } else if (hosted == ESP_OK) {
        pm_log_i(TAG, "Service: ESP-Hosted Bluetooth");
        esp_err_t hosted_bt = _init_c6_hosted_bluetooth();
        pm_log_i(TAG, "ESP-Hosted Bluetooth: %s", esp_err_to_name(hosted_bt));
    } else {
        pm_log_w(TAG, "Service: skipping BT because hosted WiFi is not ready");
    }

    pm_log_i(TAG, "System service bring-up complete");
    s_service_bringup_task = NULL;
    vTaskDelete(NULL);
}

static void _start_service_bringup(void) {
    if (s_service_bringup_task) return;
    BaseType_t ok = xTaskCreatePinnedToCore(
        _service_bringup_task,
        "pm_services",
        12288,
        NULL,
        3,
        &s_service_bringup_task,
        0
    );
    if (ok != pdPASS) {
        s_service_bringup_task = NULL;
        pm_log_w(TAG, "System service bring-up task create failed");
    }
}

// ─────────────────────────────────────────────
//  app_main — ESP-IDF entry point
// ─────────────────────────────────────────────
void app_main(void) {

    ESP_LOGI(TAG, "=== Pisces Moon OS — Pisces Moon P4 v%s ===", PM_VERSION_STRING);
    ESP_LOGI(TAG, "Boot.");

    // 1. NVS
    ESP_LOGI(TAG, "BOOTMARK: nvs_flash_init begin");
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "BOOTMARK: NVS erase required (%s)", esp_err_to_name(nvs));
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_LOGI(TAG, "BOOTMARK: nvs_flash_init done");

    // 2. HAL
    ESP_LOGI(TAG, "BOOTMARK: pm_hal_init begin");
    pm_hal_init();
    ESP_LOGI(TAG, "BOOTMARK: pm_hal_init done");

    // 3. BSP — MIPI-DSI display, GT911 touch, LVGL plumbing,
    //          backlight PWM, I2C bus. After this returns OK,
    //          screens are flushable and touch events flow.
    bool ui_ok = false;
    ESP_LOGI(TAG, "BOOTMARK: pm_bsp_init begin");
    esp_err_t bsp = pm_bsp_init();
    ESP_LOGI(TAG, "BOOTMARK: pm_bsp_init done: %s", esp_err_to_name(bsp));
    if (bsp != ESP_OK) {
        pm_log_e(TAG, "BSP init failed - running headless");
        // We continue anyway: bridge + radio paths still work
        // for diagnostics, even with no UI.
    } else {
        ESP_LOGI(TAG, "BOOTMARK: pm_bsp_start_lvgl_tick_task begin");
        pm_bsp_start_lvgl_tick_task();
        ESP_LOGI(TAG, "BOOTMARK: pm_bsp_start_lvgl_tick_task done");
        ui_ok = true;
    }

#define BOOT_STEP(label, detail, status) do { \
        if (ui_ok) { \
            pm_boot_step((label), (detail), (status)); \
            _boot_pace(PM_BOOT_STEP_PACE_MS); \
        } \
    } while (0)
#define BOOT_PROGRESS(percent) do { \
        if (ui_ok) { \
            pm_boot_progress((percent)); \
            _boot_pace(PM_BOOT_SECTION_PACE_MS); \
        } \
    } while (0)

    // BOOT SCREEN — display now alive, start streaming POST-style status
    if (ui_ok) {
        ESP_LOGI(TAG, "BOOTMARK: pm_boot_screen_show begin");
        pm_boot_screen_show();
        ESP_LOGI(TAG, "BOOTMARK: pm_boot_screen_show done");
        _boot_pace(PM_BOOT_HANDOFF_PACE_MS);
    }
    BOOT_STEP("CPU HP0",   "360 MHz RISC-V LX9",     PM_BOOT_OK);
    BOOT_STEP("CPU HP1",   "360 MHz RISC-V LX9",     PM_BOOT_OK);
    BOOT_STEP("CPU LP",    "40 MHz Sentinel Core",   PM_BOOT_OK);
    BOOT_STEP("PSRAM",     "32 MB @ 200 MHz HEX",    PM_BOOT_OK);
    BOOT_STEP("FLASH",     "16 MB DIO",              PM_BOOT_OK);
    BOOT_STEP("NVS",       "OK",                     PM_BOOT_OK);
    BOOT_STEP("HAL",       "I2C/SPI buses ready",    PM_BOOT_OK);
    bool sd_ok = pm_sd_mounted();
    BOOT_STEP("SDMMC",     sd_ok ? "microSD mounted @ /sd" : "microSD not mounted",
                 sd_ok ? PM_BOOT_OK : PM_BOOT_WARN);
    BOOT_STEP("SQLITE",    "deferred",               PM_BOOT_DISABLED);
    BOOT_STEP("MIPI-DSI",  PM_BOARD_PANEL_DETAIL,    PM_BOOT_OK);
    BOOT_STEP("GT911",     "I2C touch 400 KHz",      PM_BOOT_OK);
    BOOT_STEP("LVGL",      "v9.2 PSRAM malloc",      PM_BOOT_OK);
    BOOT_PROGRESS(45);

    pm_peer_init_base();
    BOOT_STEP("PEERS",     "base registry ready",    PM_BOOT_OK);

    // 3b. GPS source. The Cardputer ADV UART1 header bridge is the
    //     default for both 5" and 7" P4 boards. The IO52 UART path
    //     remains available on UART4 behind PM_BOARD_LOCAL_GPS_UART for later
    //     bench work, but it is parked for normal builds.
#if PM_BOARD_LOCAL_GPS_UART
    bool gps_ok = (pm_gps_uart_init() == ESP_OK);
    if (!gps_ok) {
        pm_log_w(TAG, "GPS UART init failed - continuing without GPS");
    }
    BOOT_STEP("GPS LOCAL", "UART4 IO52 @ 9600",
                 gps_ok ? PM_BOOT_OK : PM_BOOT_WARN);
#else
    BOOT_STEP("GPS SOURCE", "Cardputer ADV UART1 header", PM_BOOT_OK);
#endif
    BOOT_PROGRESS(55);

    // 3c. Optional capability providers are intentionally staged
    // after the launcher is visible. Each probe can stall or fail
    // independently without turning boot into a single long critical
    // section.
    BOOT_STEP("WIRELESS SLOT", "deferred", PM_BOOT_DISABLED);
    BOOT_PROGRESS(65);

    // 3d. Peer registry — modular ESP32 OS spine. Deferred so
    // peer-specific UART/I2C/SDIO probes cannot block the first UI.
#if PM_BOARD_CARDPUTER_RADIO_BRIDGE
    bool cardputer_armed = pm_cardputer_i2c_present();
    bool cardputer_live = pm_cardputer_i2c_link_seen();
    BOOT_STEP("CARDPUTER GPS",
                 cardputer_live ? "UART1 HELLO received" :
                 (cardputer_armed ? "UART1 armed; HELLO pending" : "UART1 arm failed"),
                 cardputer_live ? PM_BOOT_OK :
                 (cardputer_armed ? PM_BOOT_WARN : PM_BOOT_WARN));
#endif

    BOOT_STEP("T-BEAM S3", "deferred", PM_BOOT_DISABLED);
    BOOT_STEP("C6 GHOST",  "ESP-Hosted deferred",    PM_BOOT_DISABLED);
    BOOT_PROGRESS(75);

    BOOT_STEP("ESP-HOSTED", "background startup", PM_BOOT_DISABLED);
    BOOT_PROGRESS(82);

    BOOT_STEP("BLE SENSOR", "background startup", PM_BOOT_DISABLED);

    // 4. App registry — must precede launcher so categories see real counts
    main_register_apps();
    pm_log_i(TAG, "%d apps registered", pm_app_count());
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d apps / 7 categories", pm_app_count());
        BOOT_STEP("APP REGISTRY", buf, PM_BOOT_OK);
    }
    BOOT_PROGRESS(90);

    // 5. Launcher — builds category tiles with populated counts
    if (ui_ok) {
        pm_launcher_init();
    }

    // 6. C6 bridge — DISABLED on Pisces Moon P4.
    //
    // The S3-era pm_c6_bridge component uses UART transport, which is
    // wrong for this hardware. On the ELECROW CrowPanel P4, the C6 ships
    // with ESP-Hosted slave firmware and is reached transparently over
    // SDIO via standard esp_wifi_* / esp_ble_* APIs. No bridge component
    // is needed; ESP-Hosted IS the bridge.
    //
    // The S3 "Ghost Engine on Core 0" concept survives on P4 as
    // "Ghost Engine on the C6 chip" — same role, different silicon.
    //
    // Preserve the original block in #if 0 in case a custom C6 firmware
    // ever wants to use a non-ESP-Hosted protocol (e.g. promiscuous mode
    // for raw 802.11 capture). That work belongs on a separate cheap C6
    // dev board first, not this board's soldered-down C6.
#if 0
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
#endif

    BOOT_STEP("LAUNCHER", "ready",                  PM_BOOT_OK);
    BOOT_PROGRESS(100);
    _boot_pace(ui_ok ? PM_BOOT_HANDOFF_PACE_MS : 0);

    // Splash screen, then free boot memory and show launcher
    if (ui_ok) {
        ESP_LOGI(TAG, "BOOTMARK: pm_boot_splash_show begin");
        pm_boot_splash_show(PM_BOOT_SPLASH_MS);
        ESP_LOGI(TAG, "BOOTMARK: pm_boot_splash_show done");
    }

    // 7. Show launcher
    if (ui_ok) {
        pm_launcher_show();
        pm_boot_dismiss();
        _start_service_bringup();
    }

    // 8. UI loop — pinned to Core 1 to keep LVGL away from radio drain
    if (ui_ok) {
        xTaskCreatePinnedToCore(
            _ui_loop_task,
            "ui_loop",
            8192,
            NULL,
            4,
            NULL,
            1
        );
    }
    if (!ui_ok) {
        _start_service_bringup();
    }

    pm_log_i(TAG, "Boot complete. Pisces Moon P4 is running.");
    // app_main returns; FreeRTOS scheduler keeps everything alive.
#undef BOOT_PROGRESS
#undef BOOT_STEP
}
