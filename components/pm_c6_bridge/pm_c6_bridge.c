// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_c6_bridge.c — UART receiver, JSON parser, dispatcher
//
//  Reads line-delimited JSON from UART2, parses the "event"
//  field, and fans out to registered callbacks.
//
//  Uses cJSON (ESP-IDF built-in component "json") for parsing.
//
//  Backpressure: the C6 may emit faster than apps can drain.
//  We use a generous UART RX FIFO and drop overrun lines with
//  a logged warning. The C6 does not retransmit — radio
//  intelligence is lossy by nature, single-frame loss is fine.
// ============================================================

#include "pm_c6_bridge.h"
#include "pm_hal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char* TAG = "PM_C6_BRIDGE";

// ── Config ───────────────────────────────────────────────────
#define LINE_BUF_SIZE   1024
#define UART_RX_BUF     4096
#define UART_TX_BUF     1024

// ── State ────────────────────────────────────────────────────
volatile bool pm_c6_connected        = false;
volatile bool pm_c6_wardrive_active  = false;
volatile bool pm_c6_ble_active       = false;

static pm_c6_callbacks_t s_cb = {0};

// ─────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────
void pm_c6_bridge_init(const pm_c6_callbacks_t* cbs) {
    if (cbs) memcpy(&s_cb, cbs, sizeof(s_cb));

    uart_config_t cfg = {
        .baud_rate  = PM_C6_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(PM_C6_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PM_C6_UART_NUM,
                                  PM_C6_TX_PIN,
                                  PM_C6_RX_PIN,
                                  UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(PM_C6_UART_NUM,
                                         UART_RX_BUF, UART_TX_BUF,
                                         0, NULL, 0));

    ESP_LOGI(TAG, "Bridge UART%d init: %d baud TX=%d RX=%d",
             PM_C6_UART_NUM, PM_C6_BAUD,
             PM_C6_TX_PIN, PM_C6_RX_PIN);
}

// ─────────────────────────────────────────────
//  Outbound commands
// ─────────────────────────────────────────────
void pm_c6_cmd_send_raw(const char* json_line) {
    if (!json_line) return;
    uart_write_bytes(PM_C6_UART_NUM, json_line, strlen(json_line));
    if (json_line[strlen(json_line) - 1] != '\n') {
        uart_write_bytes(PM_C6_UART_NUM, "\n", 1);
    }
}

void pm_c6_cmd_wardrive_start(void)    { pm_c6_cmd_send_raw("{\"cmd\":\"wardrive_start\"}");    pm_c6_wardrive_active = true; }
void pm_c6_cmd_wardrive_stop(void)     { pm_c6_cmd_send_raw("{\"cmd\":\"wardrive_stop\"}");     pm_c6_wardrive_active = false; }
void pm_c6_cmd_ble_start(void)         { pm_c6_cmd_send_raw("{\"cmd\":\"ble_start\"}");         pm_c6_ble_active = true; }
void pm_c6_cmd_ble_stop(void)          { pm_c6_cmd_send_raw("{\"cmd\":\"ble_stop\"}");          pm_c6_ble_active = false; }
void pm_c6_cmd_promiscuous_start(void) { pm_c6_cmd_send_raw("{\"cmd\":\"promiscuous_start\"}"); }
void pm_c6_cmd_promiscuous_stop(void)  { pm_c6_cmd_send_raw("{\"cmd\":\"promiscuous_stop\"}");  }
void pm_c6_cmd_raw_log_start(void)     { pm_c6_cmd_send_raw("{\"cmd\":\"raw_log_start\"}");     }
void pm_c6_cmd_raw_log_stop(void)      { pm_c6_cmd_send_raw("{\"cmd\":\"raw_log_stop\"}");      }
void pm_c6_cmd_ping(void)              { pm_c6_cmd_send_raw("{\"cmd\":\"ping\"}");              }
void pm_c6_cmd_status(void)            { pm_c6_cmd_send_raw("{\"cmd\":\"status\"}");            }

// ─────────────────────────────────────────────
//  JSON helpers — safe accessors with defaults
// ─────────────────────────────────────────────
static const char* _js_str(cJSON* root, const char* key, const char* dflt) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(v) && v->valuestring) return v->valuestring;
    return dflt;
}

static int _js_int(cJSON* root, const char* key, int dflt) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(v)) return v->valueint;
    return dflt;
}

static double _js_dbl(cJSON* root, const char* key, double dflt) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(v)) return v->valuedouble;
    return dflt;
}

static bool _js_bool(cJSON* root, const char* key, bool dflt) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    return dflt;
}

// ─────────────────────────────────────────────
//  Dispatch one parsed event
// ─────────────────────────────────────────────
static void _dispatch_event(cJSON* root) {
    const char* event = _js_str(root, "event", "");

    if (strcmp(event, "wifi_seen") == 0) {
        if (s_cb.on_wifi) {
            s_cb.on_wifi(
                _js_str(root, "mac",  ""),
                _js_str(root, "ssid", ""),
                _js_int(root, "rssi", 0),
                _js_int(root, "ch",   0),
                _js_str(root, "enc",  "OPEN"),
                _js_dbl(root, "lat",  0.0),
                _js_dbl(root, "lng",  0.0));
        }
    }
    else if (strcmp(event, "ble_seen") == 0) {
        if (s_cb.on_ble) {
            s_cb.on_ble(
                _js_str(root, "mac",       ""),
                _js_str(root, "name",      ""),
                _js_int(root, "rssi",      0),
                _js_dbl(root, "lat",       0.0),
                _js_dbl(root, "lng",       0.0),
                _js_str(root, "addr_type", "public"),
                _js_str(root, "mfg_data",  ""));
        }
    }
    else if (strcmp(event, "probe_seen") == 0) {
        if (s_cb.on_probe) {
            s_cb.on_probe(
                _js_str(root, "mac",  ""),
                _js_str(root, "ssid", ""),
                _js_int(root, "rssi", 0),
                _js_int(root, "count",1),
                _js_dbl(root, "lat",  0.0),
                _js_dbl(root, "lng",  0.0));
        }
    }
    else if (strcmp(event, "gps") == 0) {
        // NOTE: As of Phase 13, GPS is read directly by the P4
        // (pm_gps_uart). The C6 firmware no longer emits gps
        // events. We keep this handler so that legacy/test C6
        // builds can still feed pm_gps_state if encountered.
        if (s_cb.on_gps) {
            s_cb.on_gps(
                _js_dbl(root, "lat",   0.0),
                _js_dbl(root, "lng",   0.0),
                _js_dbl(root, "alt_m", 0.0),
                _js_int(root, "sats",  0),
                _js_bool(root,"valid", false));
        }
    }
    else if (strcmp(event, "pkt") == 0) {
        if (s_cb.on_pkt) {
            s_cb.on_pkt(
                _js_str(root, "frame_type", ""),
                _js_str(root, "src",        ""),
                _js_int(root, "rssi",       0));
        }
    }
    else if (strcmp(event, "ready") == 0) {
        pm_c6_connected = true;
        if (s_cb.on_ready) {
            s_cb.on_ready(
                _js_str(root, "firmware", ""),
                _js_str(root, "version",  ""));
        }
    }
    else if (strcmp(event, "status") == 0) {
        if (s_cb.on_status) {
            s_cb.on_status(
                _js_bool(root, "wifi_active",     false),
                _js_bool(root, "ble_active",      false),
                _js_int (root, "networks_found",  0),
                _js_int (root, "ble_devices",     0),
                (uint32_t)_js_int(root, "uptime_s", 0));
        }
    }
    else if (strcmp(event, "pong") == 0) {
        ESP_LOGI(TAG, "C6 pong");
    }
    else {
        ESP_LOGD(TAG, "unhandled event '%s'", event);
    }
}

// ─────────────────────────────────────────────
//  Receiver task — line buffer over UART
// ─────────────────────────────────────────────
void pm_c6_bridge_task(void* pvArgs) {
    (void)pvArgs;
    static char line[LINE_BUF_SIZE];
    int  linelen = 0;
    uint8_t rxbuf[256];

    ESP_LOGI(TAG, "Bridge receiver task running");

    while (1) {
        int len = uart_read_bytes(PM_C6_UART_NUM, rxbuf,
                                   sizeof(rxbuf),
                                   pdMS_TO_TICKS(20));
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)rxbuf[i];
            if (c == '\r') continue;
            if (c == '\n' || linelen >= LINE_BUF_SIZE - 1) {
                line[linelen] = 0;
                if (linelen > 0) {
                    cJSON* root = cJSON_Parse(line);
                    if (root) {
                        _dispatch_event(root);
                        cJSON_Delete(root);
                    } else {
                        ESP_LOGW(TAG, "JSON parse failed (%d bytes)", linelen);
                    }
                }
                linelen = 0;
            } else {
                line[linelen++] = c;
            }
        }
    }
}
