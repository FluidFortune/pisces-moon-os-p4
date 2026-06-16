// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_c5_uart.c — ESP32-C5 edge-radio peer over UART
//
//  Implementation notes:
//
//  This is structurally a stripped, dedicated UART transport
//  modelled on the bridge half of pm_cardputer_i2c. The
//  Cardputer module carries a legacy I2C register protocol
//  behind the same API; the C5 has no I2C path, so we keep
//  this file UART-only.
//
//  The PMU1 wire protocol is identical, so the parser, the
//  hex codec, and the queue shapes are clones of the Cardputer
//  versions. They diverge only in:
//    - UART_NUM_2 instead of UART_NUM_1
//    - GPIO 45/46 (P4 I2C1 connector) instead of 48/47
//    - A new capability bit, PM_C5_CAP_WIFI_5GHZ
//    - Its own task, queues, and write mutex (no sharing)
//    - "PMU1 CMD wifi_promisc_start_5ghz" CMD variant for the
//      dual-band scan helpers
// ============================================================

#include "pm_c5_uart.h"

#include "pm_hal.h"

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_C5_UART";

// ── Tuning ───────────────────────────────────────────────────
#define PM_C5_UART_NUM             UART_NUM_2
#define PM_C5_UART_RX_BYTES        4096
#define PM_C5_UART_TX_BYTES        1024
#define PM_C5_UART_LINE_MAX        640
#define PM_C5_UART_TASK_STACK      8192

#define PM_C5_UART_WIFI_Q_DEPTH    16
#define PM_C5_UART_LORA_Q_DEPTH    4
#define PM_C5_UART_BLE_Q_DEPTH     24

// ── Module state ─────────────────────────────────────────────
static bool         s_started     = false;
static bool         s_link_seen   = false;
static uint32_t     s_caps        = 0;
static uint32_t     s_rx_bytes    = 0;
static TaskHandle_t s_rx_task     = NULL;
static SemaphoreHandle_t s_write_mutex = NULL;
static portMUX_TYPE s_qmux        = portMUX_INITIALIZER_UNLOCKED;

static pm_cardputer_i2c_wifi_frame_t s_wifi_q[PM_C5_UART_WIFI_Q_DEPTH];
static uint8_t s_wifi_head = 0, s_wifi_tail = 0, s_wifi_count = 0;

static pm_cardputer_i2c_lora_rx_t s_lora_q[PM_C5_UART_LORA_Q_DEPTH];
static uint8_t s_lora_head = 0, s_lora_tail = 0, s_lora_count = 0;

static pm_cardputer_i2c_ble_seen_t s_ble_q[PM_C5_UART_BLE_Q_DEPTH];
static uint8_t s_ble_head = 0, s_ble_tail = 0, s_ble_count = 0;

static bool s_ble_scan_requested = false;
static bool s_ble_scan_active_mode = false;

// ── Hex codec (parity with pm_cardputer_i2c) ─────────────────
static int _hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t _hex_decode(const char* hex, uint8_t* out, size_t out_len) {
    if (!hex || !out || out_len == 0) return 0;
    size_t n = 0;
    while (hex[0] && hex[1] && n < out_len) {
        if (hex[0] == ' ' || hex[0] == '\r' || hex[0] == '\n') break;
        int hi = _hex_nibble(hex[0]);
        int lo = _hex_nibble(hex[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

// ── Key=value parsing ────────────────────────────────────────
static const char* _kv_value(const char* line, const char* key) {
    if (!line || !key) return NULL;
    size_t key_len = strlen(key);
    const char* p = line;
    while ((p = strstr(p, key)) != NULL) {
        bool left_ok = (p == line || p[-1] == ' ');
        bool right_ok = (p[key_len] == '=');
        if (left_ok && right_ok) return p + key_len + 1;
        p += key_len;
    }
    return NULL;
}

static long _kv_long(const char* line, const char* key, long fallback) {
    const char* v = _kv_value(line, key);
    if (!v) return fallback;
    char* end = NULL;
    long n = strtol(v, &end, 0);
    return (end && end != v) ? n : fallback;
}

static unsigned long _kv_ulong(const char* line, const char* key,
                                unsigned long fallback) {
    const char* v = _kv_value(line, key);
    if (!v) return fallback;
    char* end = NULL;
    unsigned long n = strtoul(v, &end, 0);
    return (end && end != v) ? n : fallback;
}

static void _kv_token(const char* line, const char* key,
                       char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = 0;
    const char* v = _kv_value(line, key);
    if (!v) return;
    size_t n = 0;
    while (v[n] && v[n] != ' ' && v[n] != '\r' && v[n] != '\n' &&
           n + 1 < out_len) {
        out[n] = v[n];
        n++;
    }
    out[n] = 0;
}

static void _normalize_mac(const char* raw, char* out, size_t out_len) {
    if (!out || out_len < 18) return;
    out[0] = 0;
    if (!raw) return;
    char hex[13] = {0};
    int n = 0;
    for (const char* p = raw; *p && n < 12; p++) {
        if (isxdigit((unsigned char)*p)) {
            hex[n++] = (char)toupper((unsigned char)*p);
        }
    }
    if (n != 12) return;
    snprintf(out, out_len, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             hex[0], hex[1], hex[2], hex[3], hex[4], hex[5],
             hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);
}

// ── Queue helpers ────────────────────────────────────────────
static void _push_wifi(const pm_cardputer_i2c_wifi_frame_t* f) {
    if (!f || !f->available) return;
    portENTER_CRITICAL(&s_qmux);
    if (s_wifi_count < PM_C5_UART_WIFI_Q_DEPTH) {
        s_wifi_q[s_wifi_head] = *f;
        s_wifi_head = (uint8_t)((s_wifi_head + 1) % PM_C5_UART_WIFI_Q_DEPTH);
        s_wifi_count++;
    }
    portEXIT_CRITICAL(&s_qmux);
}

static bool _pop_wifi(pm_cardputer_i2c_wifi_frame_t* out) {
    bool ok = false;
    if (!out) return false;
    portENTER_CRITICAL(&s_qmux);
    if (s_wifi_count > 0) {
        *out = s_wifi_q[s_wifi_tail];
        s_wifi_tail = (uint8_t)((s_wifi_tail + 1) % PM_C5_UART_WIFI_Q_DEPTH);
        s_wifi_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_qmux);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

static void _push_lora(const pm_cardputer_i2c_lora_rx_t* f) {
    if (!f || !f->available) return;
    portENTER_CRITICAL(&s_qmux);
    if (s_lora_count < PM_C5_UART_LORA_Q_DEPTH) {
        s_lora_q[s_lora_head] = *f;
        s_lora_head = (uint8_t)((s_lora_head + 1) % PM_C5_UART_LORA_Q_DEPTH);
        s_lora_count++;
    }
    portEXIT_CRITICAL(&s_qmux);
}

static bool _pop_lora(pm_cardputer_i2c_lora_rx_t* out) {
    bool ok = false;
    if (!out) return false;
    portENTER_CRITICAL(&s_qmux);
    if (s_lora_count > 0) {
        *out = s_lora_q[s_lora_tail];
        s_lora_tail = (uint8_t)((s_lora_tail + 1) % PM_C5_UART_LORA_Q_DEPTH);
        s_lora_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_qmux);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

static void _push_ble(const pm_cardputer_i2c_ble_seen_t* b) {
    if (!b || !b->available || !b->mac[0]) return;
    portENTER_CRITICAL(&s_qmux);
    if (s_ble_count < PM_C5_UART_BLE_Q_DEPTH) {
        s_ble_q[s_ble_head] = *b;
        s_ble_head = (uint8_t)((s_ble_head + 1) % PM_C5_UART_BLE_Q_DEPTH);
        s_ble_count++;
    } else {
        // Overflow: drop the oldest so the freshest survives.
        s_ble_q[s_ble_tail] = *b;
        s_ble_tail = (uint8_t)((s_ble_tail + 1) % PM_C5_UART_BLE_Q_DEPTH);
        s_ble_head = s_ble_tail;
    }
    portEXIT_CRITICAL(&s_qmux);
}

static bool _pop_ble(pm_cardputer_i2c_ble_seen_t* out) {
    bool ok = false;
    if (!out) return false;
    portENTER_CRITICAL(&s_qmux);
    if (s_ble_count > 0) {
        *out = s_ble_q[s_ble_tail];
        s_ble_tail = (uint8_t)((s_ble_tail + 1) % PM_C5_UART_BLE_Q_DEPTH);
        s_ble_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_qmux);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

// ── Outbound write ───────────────────────────────────────────
static esp_err_t _write_line(const char* line) {
    if (!s_started || !line) return ESP_ERR_INVALID_STATE;
    bool locked = false;
    if (s_write_mutex) {
        if (xSemaphoreTake(s_write_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        locked = true;
    }
    int n = uart_write_bytes(PM_C5_UART_NUM, line, strlen(line));
    if (locked) xSemaphoreGive(s_write_mutex);
    return n >= 0 ? ESP_OK : ESP_FAIL;
}

// ── Inbound line handler ─────────────────────────────────────
static void _handle_line(const char* line) {
    if (!line || strncmp(line, "PMU1 ", 5) != 0) return;

    if (strncmp(line + 5, "HELLO", 5) == 0) {
        uint32_t new_caps = (uint32_t)_kv_ulong(line, "caps",
                                                 PM_CARDPUTER_CAP_WIFI |
                                                 PM_CARDPUTER_CAP_BLE  |
                                                 PM_CARDPUTER_CAP_WIFI_SCAN |
                                                 PM_C5_CAP_WIFI_5GHZ);
        bool first = !s_link_seen;
        if (first || new_caps != s_caps) {
            ESP_LOGI(TAG, "C5 UART hello caps=0x%08lx", (unsigned long)new_caps);
        }
        s_caps = new_caps;
        s_link_seen = true;
        return;
    }

    s_link_seen = true;

    if (strncmp(line + 5, "WF", 2) == 0) {
        pm_cardputer_i2c_wifi_frame_t f = {
            .available  = 1,
            .frame_type = (uint8_t)_kv_ulong(line, "type", 0),
            .channel    = (uint8_t)_kv_ulong(line, "ch", 0),
            .rssi       = (int8_t)_kv_long(line, "rssi", 0),
            .len        = (uint16_t)_kv_ulong(line, "len", 0),
        };
        const char* mac = _kv_value(line, "mac");
        if (mac && strlen(mac) >= 12) {
            for (int i = 0; i < 6; i++) {
                int hi = _hex_nibble(mac[i * 2]);
                int lo = _hex_nibble(mac[i * 2 + 1]);
                f.mac[i] = (hi >= 0 && lo >= 0)
                            ? (uint8_t)((hi << 4) | lo) : 0;
            }
        }
        const char* data = _kv_value(line, "data");
        size_t got = _hex_decode(data, f.data, sizeof(f.data));
        if (f.len == 0 || f.len > got) f.len = (uint16_t)got;
        _push_wifi(&f);
        return;
    }

    if (strncmp(line + 5, "LORA", 4) == 0) {
        pm_cardputer_i2c_lora_rx_t rx = {
            .available = 1,
            .rssi      = (int8_t)_kv_long(line, "rssi", 0),
            .snr_x4    = (int8_t)_kv_long(line, "snr_x4", 0),
            .freq_khz  = (uint32_t)_kv_ulong(line, "freq", 0),
        };
        const char* data = _kv_value(line, "data");
        size_t got = _hex_decode(data, rx.data, sizeof(rx.data));
        rx.len = (uint8_t)got;
        _push_lora(&rx);
        return;
    }

    if (strncmp(line + 5, "BLE", 3) == 0) {
        pm_cardputer_i2c_ble_seen_t b = {
            .available = 1,
            .rssi      = (int8_t)_kv_long(line, "rssi", 0),
        };
        char raw_mac[24] = {0};
        _kv_token(line, "mac", raw_mac, sizeof(raw_mac));
        _normalize_mac(raw_mac, b.mac, sizeof(b.mac));
        _kv_token(line, "type", b.addr_type, sizeof(b.addr_type));
        if (!b.addr_type[0]) snprintf(b.addr_type, sizeof(b.addr_type),
                                       "remote");
        _kv_token(line, "mfg", b.mfg, sizeof(b.mfg));
        _kv_token(line, "name", b.name, sizeof(b.name));
        if (b.mac[0]) _push_ble(&b);
        return;
    }

    // GPS, KEY, and other lines are intentionally ignored on the
    // C5 link. The C5 has no GPS or keyboard — only radios. If a
    // future C5 variant adds GPS we can mirror it into pm_gps_state
    // the same way pm_cardputer_i2c does.
}

// ── RX task ──────────────────────────────────────────────────
//
// Reads bytes one at a time, assembles lines on '\n', and feeds
// each into _handle_line. Sends a PMU1 PING every second so the
// C5 knows the link is alive. Logs a diagnostic if bytes are
// flowing but HELLO has not yet been seen (wrong baud, wrong
// pinning, C5 not running PM Edge yet).
static void _rx_task(void* arg) {
    (void)arg;
    char line[PM_C5_UART_LINE_MAX];
    size_t pos = 0;
    uint32_t last_ping = 0;
    uint32_t last_diag = 0;
    uint32_t last_diag_bytes = 0;

    while (s_started) {
        uint8_t ch = 0;
        int n = uart_read_bytes(PM_C5_UART_NUM, &ch, 1, pdMS_TO_TICKS(20));
        if (n > 0) {
            s_rx_bytes += (uint32_t)n;
            if (ch == '\n') {
                line[pos] = 0;
                if (pos > 0 && line[pos - 1] == '\r') line[pos - 1] = 0;
                _handle_line(line);
                pos = 0;
            } else if (pos + 1 < sizeof(line)) {
                line[pos++] = (char)ch;
            } else {
                pos = 0;   // overflow — drop and resync on next '\n'
            }
        }

        uint32_t now = pm_millis();
        if (now - last_ping > 1000) {
            _write_line("PMU1 PING\n");
            last_ping = now;
        }
        if (!s_link_seen && now - last_diag > 5000) {
            ESP_LOGW(TAG, "C5 UART waiting for HELLO: rx_bytes=%lu (+%lu) "
                          "RX=IO%d TX=IO%d baud=%d",
                     (unsigned long)s_rx_bytes,
                     (unsigned long)(s_rx_bytes - last_diag_bytes),
                     PM_C5_UART_RX_PIN, PM_C5_UART_TX_PIN,
                     PM_C5_UART_BAUD);
            last_diag_bytes = s_rx_bytes;
            last_diag = now;
        }
    }

    s_rx_task = NULL;
    vTaskDelete(NULL);
}

// ── Public API ───────────────────────────────────────────────
bool pm_c5_uart_init(void) {
    if (s_started) return true;

    uart_config_t cfg = {
        .baud_rate  = PM_C5_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(PM_C5_UART_NUM, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_set_pin(PM_C5_UART_NUM,
                       PM_C5_UART_TX_PIN,
                       PM_C5_UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_driver_install(PM_C5_UART_NUM,
                              PM_C5_UART_RX_BYTES,
                              PM_C5_UART_TX_BYTES,
                              0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    if (!s_write_mutex) {
        s_write_mutex = xSemaphoreCreateMutex();
        if (!s_write_mutex) return false;
    }

    s_started = true;
    xTaskCreatePinnedToCore(_rx_task, "pm_c5_uart",
                            PM_C5_UART_TASK_STACK,
                            NULL, 4, &s_rx_task, 0);

    ESP_LOGI(TAG, "C5 UART bridge armed: RX=IO%d TX=IO%d baud=%d",
             PM_C5_UART_RX_PIN, PM_C5_UART_TX_PIN, PM_C5_UART_BAUD);
    return true;
}

bool pm_c5_uart_present(void)    { return s_started; }
bool pm_c5_uart_link_seen(void)  { return s_started && s_link_seen; }
uint32_t pm_c5_uart_caps(void)   { return s_link_seen ? s_caps : 0; }
uint32_t pm_c5_uart_rx_bytes(void) { return s_rx_bytes; }

esp_err_t pm_c5_uart_ble_scan_start(bool active) {
    char line[96];
    snprintf(line, sizeof(line),
             "PMU1 CMD ble_scan_start active=%u interval=100 window=80\n",
             active ? 1u : 0u);
    esp_err_t err = _write_line(line);
    if (err == ESP_OK) {
        s_ble_scan_requested = true;
        s_ble_scan_active_mode = active;
    }
    return err;
}

esp_err_t pm_c5_uart_ble_scan_stop(void) {
    esp_err_t err = _write_line("PMU1 CMD ble_scan_stop\n");
    if (err == ESP_OK) s_ble_scan_requested = false;
    return err;
}

static esp_err_t _wifi_ctrl(const char* cmd, uint8_t channel, uint8_t filter) {
    char line[96];
    snprintf(line, sizeof(line),
             "PMU1 CMD %s ch=%u filter=%u\n", cmd, channel, filter);
    return _write_line(line);
}

esp_err_t pm_c5_uart_wifi_promisc_start(uint8_t channel, uint8_t filter) {
    return _wifi_ctrl("wifi_promisc_start", channel, filter);
}

esp_err_t pm_c5_uart_wifi_promisc_stop(void) {
    return _wifi_ctrl("wifi_promisc_stop", 0, 0);
}

esp_err_t pm_c5_uart_wifi_set_channel(uint8_t channel) {
    return _wifi_ctrl("wifi_set_channel", channel, 0);
}

// 5 GHz variant — same CMD shape, separate verb so the C5 picks
// the right band. Channel numbers above 14 implicitly fall in the
// 5 GHz UNII bands; the C5 firmware uses the channel number to
// choose the band, so this is mostly a clarity wrapper.
esp_err_t pm_c5_uart_wifi_promisc_start_5ghz(uint8_t channel, uint8_t filter) {
    char line[96];
    snprintf(line, sizeof(line),
             "PMU1 CMD wifi_promisc_start_5ghz ch=%u filter=%u\n",
             channel, filter);
    return _write_line(line);
}

esp_err_t pm_c5_uart_wifi_frame_pop(pm_cardputer_i2c_wifi_frame_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (_pop_wifi(out)) return ESP_OK;
    memset(out, 0, sizeof(*out));
    return ESP_OK;
}

esp_err_t pm_c5_uart_ble_seen_pop(pm_cardputer_i2c_ble_seen_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (_pop_ble(out)) return ESP_OK;
    memset(out, 0, sizeof(*out));
    return ESP_OK;
}

esp_err_t pm_c5_uart_lora_rx_pop(pm_cardputer_i2c_lora_rx_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (_pop_lora(out)) return ESP_OK;
    memset(out, 0, sizeof(*out));
    return ESP_OK;
}

// ── Required-capability gate for dispatched ops ──────────────
static uint32_t _required_cap_for_op(const char* op) {
    if (!op) return 0;
    if (strncmp(op, "lora_", 5) == 0)        return PM_CARDPUTER_CAP_LORA;
    if (strncmp(op, "ble_",  4) == 0)        return PM_CARDPUTER_CAP_BLE;
    if (strcmp(op, "wifi_promisc_start_5ghz") == 0) {
        return PM_C5_CAP_WIFI_5GHZ;
    }
    if (strncmp(op, "wifi_", 5) == 0)        return PM_CARDPUTER_CAP_WIFI_PROMISC;
    return 0;
}

static int _extract_uint_param(const char* params, const char* key,
                                 int fallback) {
    if (!params || !key) return fallback;
    const char* p = strstr(params, key);
    if (!p) return fallback;
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\"' || *p == '\'') p++;
    int v = fallback;
    if (sscanf(p, "%d", &v) != 1) return fallback;
    if (v < 0) return fallback;
    return v;
}

int pm_c5_uart_call(const char* op, const char* params) {
    if (!op || !s_started) return -1;

    if (strcmp(op, "ping") == 0) {
        return _write_line("PMU1 PING\n") == ESP_OK ? 0 : -2;
    }

    if (strcmp(op, "status") == 0) {
        ESP_LOGI(TAG, "status started=%d seen=%d caps=0x%08lx "
                       "wifi_q=%u lora_q=%u ble_q=%u",
                 s_started, s_link_seen, (unsigned long)s_caps,
                 s_wifi_count, s_lora_count, s_ble_count);
        return 0;
    }

    if (!s_link_seen) {
        ESP_LOGW(TAG, "C5 op '%s' requested before HELLO", op);
        return -5;
    }
    uint32_t need = _required_cap_for_op(op);
    if (need && !(s_caps & need)) {
        ESP_LOGW(TAG, "C5 op '%s' missing caps=0x%08lx need=0x%08lx",
                 op, (unsigned long)s_caps, (unsigned long)need);
        return -6;
    }

    if (strcmp(op, "ble_scan_start") == 0) {
        bool active = _extract_uint_param(params, "active", 0) != 0;
        return pm_c5_uart_ble_scan_start(active) == ESP_OK ? 0 : -2;
    }
    if (strcmp(op, "ble_scan_stop") == 0) {
        return pm_c5_uart_ble_scan_stop() == ESP_OK ? 0 : -2;
    }
    if (strcmp(op, "ble_seen_pop") == 0) {
        pm_cardputer_i2c_ble_seen_t b = {0};
        esp_err_t err = pm_c5_uart_ble_seen_pop(&b);
        if (err != ESP_OK) return -2;
        return b.available ? 0 : 1;
    }

    if (strcmp(op, "wifi_promisc_start") == 0 ||
        strcmp(op, "promiscuous_start") == 0) {
        uint8_t ch  = (uint8_t)_extract_uint_param(params, "channel", 0);
        uint8_t flt = (uint8_t)_extract_uint_param(params, "filter", 0xff);
        return pm_c5_uart_wifi_promisc_start(ch, flt) == ESP_OK ? 0 : -2;
    }
    if (strcmp(op, "wifi_promisc_start_5ghz") == 0) {
        uint8_t ch  = (uint8_t)_extract_uint_param(params, "channel", 36);
        uint8_t flt = (uint8_t)_extract_uint_param(params, "filter", 0xff);
        return pm_c5_uart_wifi_promisc_start_5ghz(ch, flt) == ESP_OK ? 0 : -2;
    }
    if (strcmp(op, "wifi_promisc_stop") == 0 ||
        strcmp(op, "promiscuous_stop") == 0) {
        return pm_c5_uart_wifi_promisc_stop() == ESP_OK ? 0 : -2;
    }
    if (strcmp(op, "wifi_set_channel") == 0 ||
        strcmp(op, "promiscuous_set_channel") == 0) {
        uint8_t ch = (uint8_t)_extract_uint_param(params, "channel", 1);
        return pm_c5_uart_wifi_set_channel(ch) == ESP_OK ? 0 : -2;
    }
    if (strcmp(op, "wifi_frame_pop") == 0) {
        pm_cardputer_i2c_wifi_frame_t f = {0};
        esp_err_t err = pm_c5_uart_wifi_frame_pop(&f);
        if (err != ESP_OK) return -2;
        return f.available ? 0 : 1;
    }

    if (strcmp(op, "lora_rx_pop") == 0) {
        pm_cardputer_i2c_lora_rx_t r = {0};
        esp_err_t err = pm_c5_uart_lora_rx_pop(&r);
        if (err != ESP_OK) return -2;
        return r.available ? 0 : 1;
    }

    ESP_LOGW(TAG, "unknown C5 op '%s'", op);
    return -4;
}
