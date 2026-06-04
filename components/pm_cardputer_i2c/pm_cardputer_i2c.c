// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#include "pm_cardputer_i2c.h"

#include "pm_board.h"
#include "pm_bsp.h"
#include "pm_gps_state.h"
#include "pm_hal.h"
#include "pm_input.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_CARDPUTER";

#define PM_CARDPUTER_I2C_TIMEOUT_MS 80
#define PM_CARDPUTER_KEY_POLL_MS    45
#define PM_CARDPUTER_GPS_POLL_MS    1000

// CrowPanel Advanced P4 physical UART1 connector (RX1/TX1/3V3/GND).
// The Eagle schematic maps RXD1/TXD1 to ESP32-P4 GPIO48/GPIO47.
#define PM_CARDPUTER_UART_NUM       UART_NUM_1
#define PM_CARDPUTER_UART_RX_PIN    48
#define PM_CARDPUTER_UART_TX_PIN    47
#define PM_CARDPUTER_UART_BAUD      921600
#define PM_CARDPUTER_UART_RX_BYTES  4096
#define PM_CARDPUTER_UART_TX_BYTES  1024
#define PM_CARDPUTER_UART_LINE_MAX  640
#define PM_CARDPUTER_UART_TASK_STACK 8192
#define PM_CARDPUTER_UART_WIFI_Q_DEPTH 16
#define PM_CARDPUTER_UART_LORA_Q_DEPTH 4
#define PM_CARDPUTER_UART_BLE_Q_DEPTH 24
#define PM_CARDPUTER_KEY_MOD_LSHIFT 0x02
#define PM_CARDPUTER_KEY_MOD_RSHIFT 0x20

static bool         s_present = false;
static uint32_t     s_caps = 0;
static char         s_name[PM_CARDPUTER_I2C_NAME_LEN + 1];
static TaskHandle_t s_key_task = NULL;
static TaskHandle_t s_gps_task = NULL;
static TaskHandle_t s_uart_task = NULL;
static bool         s_uart_started = false;
static bool         s_uart_seen = false;
static uint32_t     s_uart_rx_bytes = 0;
static portMUX_TYPE s_uart_mux = portMUX_INITIALIZER_UNLOCKED;
static pm_cardputer_i2c_wifi_frame_t s_uart_wifi_q[PM_CARDPUTER_UART_WIFI_Q_DEPTH];
static uint8_t s_uart_wifi_head = 0;
static uint8_t s_uart_wifi_tail = 0;
static uint8_t s_uart_wifi_count = 0;
static pm_cardputer_i2c_lora_rx_t s_uart_lora_q[PM_CARDPUTER_UART_LORA_Q_DEPTH];
static uint8_t s_uart_lora_head = 0;
static uint8_t s_uart_lora_tail = 0;
static uint8_t s_uart_lora_count = 0;
static pm_cardputer_i2c_ble_seen_t s_uart_ble_q[PM_CARDPUTER_UART_BLE_Q_DEPTH];
static uint8_t s_uart_ble_head = 0;
static uint8_t s_uart_ble_tail = 0;
static uint8_t s_uart_ble_count = 0;
static SemaphoreHandle_t s_uart_write_mutex = NULL;
static bool s_uart_ble_scan_requested = false;
static bool s_uart_ble_scan_active_mode = false;

static void _uart_hex_encode(const uint8_t* data, size_t len, char* out, size_t out_len);
static esp_err_t _uart_write_line(const char* line);
static esp_err_t _uart_ble_scan_start_raw(bool active);
static esp_err_t _uart_ble_scan_stop_raw(void);
static bool _pop_uart_wifi(pm_cardputer_i2c_wifi_frame_t* out);
static bool _pop_uart_lora(pm_cardputer_i2c_lora_rx_t* out);
static bool _pop_uart_ble(pm_cardputer_i2c_ble_seen_t* out);
static esp_err_t _uart_bridge_start(void);

static uint32_t _default_caps(void) {
    return PM_CARDPUTER_CAP_KEYBOARD |
           PM_CARDPUTER_CAP_GPS |
           PM_CARDPUTER_CAP_LORA |
           PM_CARDPUTER_CAP_WIFI |
           PM_CARDPUTER_CAP_BLE |
           PM_CARDPUTER_CAP_WIFI_PROMISC |
           PM_CARDPUTER_CAP_WIFI_SCAN;
}

static bool _valid_magic(uint8_t m0, uint8_t m1, uint8_t version) {
    return m0 == 'P' && m1 == 'M' && version == PM_CARDPUTER_I2C_VERSION;
}

static esp_err_t _read_reg(uint8_t reg, void* out, size_t len) {
    if (!out || len == 0) return ESP_ERR_INVALID_ARG;
    return pm_bsp_i2c_transmit_receive(PM_CARDPUTER_I2C_ADDR,
                                       &reg, 1,
                                       (uint8_t*)out, len,
                                       PM_CARDPUTER_I2C_TIMEOUT_MS);
}

esp_err_t pm_cardputer_i2c_read_whoami(pm_cardputer_i2c_whoami_t* out) {
    esp_err_t err = _read_reg(PM_CARDPUTER_REG_WHOAMI, out, sizeof(*out));
    if (err != ESP_OK) return err;
    if (!_valid_magic(out->magic0, out->magic1, out->version)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t pm_cardputer_i2c_read_status(pm_cardputer_i2c_status_t* out) {
    esp_err_t err = _read_reg(PM_CARDPUTER_REG_STATUS, out, sizeof(*out));
    if (err != ESP_OK) return err;
    if (!_valid_magic(out->magic0, out->magic1, out->version)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t pm_cardputer_i2c_read_gps(pm_cardputer_i2c_gps_t* out) {
    return _read_reg(PM_CARDPUTER_REG_GPS, out, sizeof(*out));
}

esp_err_t pm_cardputer_i2c_poll_key(pm_cardputer_i2c_key_t* out) {
    return _read_reg(PM_CARDPUTER_REG_KEY_POP, out, sizeof(*out));
}

esp_err_t pm_cardputer_i2c_lora_tx(const uint8_t* data, uint8_t len) {
    if (!data || len == 0 || len > PM_CARDPUTER_I2C_LORA_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    char hex[PM_CARDPUTER_I2C_LORA_MAX * 2 + 1];
    char line[PM_CARDPUTER_I2C_LORA_MAX * 2 + 48];
    _uart_hex_encode(data, len, hex, sizeof(hex));
    snprintf(line, sizeof(line), "PMU1 CMD lora_tx len=%u data=%s\n", len, hex);
    bool resume_ble = s_uart_ble_scan_requested;
    if (resume_ble) {
        _uart_ble_scan_stop_raw();
        pm_delay_ms(15);
    }
    esp_err_t err = _uart_write_line(line);
    if (resume_ble) {
        pm_delay_ms(25);
        _uart_ble_scan_start_raw(s_uart_ble_scan_active_mode);
    }
    if (err == ESP_OK) {
        pm_log_i(TAG, "Cardputer LoRa TX queued len=%u", len);
    }
    return err;
#else

    pm_cardputer_i2c_lora_tx_t frame = {
        .reg = PM_CARDPUTER_REG_LORA_TX,
        .len = len,
    };
    memcpy(frame.data, data, len);
    return pm_bsp_i2c_transmit(PM_CARDPUTER_I2C_ADDR,
                               (const uint8_t*)&frame,
                               (size_t)len + 2,
                               PM_CARDPUTER_I2C_TIMEOUT_MS);
#endif
}

bool pm_cardputer_i2c_present(void) {
    return s_present;
}

bool pm_cardputer_i2c_link_seen(void) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    return s_present && s_uart_seen;
#else
    return s_present;
#endif
}

uint32_t pm_cardputer_i2c_caps(void) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    return s_uart_seen ? s_caps : 0;
#else
    return s_caps;
#endif
}

esp_err_t pm_cardputer_i2c_lora_rx_pop(pm_cardputer_i2c_lora_rx_t* out) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    if (!out) return ESP_ERR_INVALID_ARG;
    if (_pop_uart_lora(out)) return ESP_OK;
    memset(out, 0, sizeof(*out));
    return ESP_OK;
#else
    return _read_reg(PM_CARDPUTER_REG_LORA_RX_POP, out, sizeof(*out));
#endif
}

static esp_err_t _wifi_ctrl(uint8_t op, uint8_t channel, uint8_t filter) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    char line[96];
    const char* cmd = "wifi_promisc_stop";
    if (op == PM_CARDPUTER_WIFI_OP_PROMISC_START) {
        cmd = "wifi_promisc_start";
    } else if (op == PM_CARDPUTER_WIFI_OP_SET_CHANNEL) {
        cmd = "wifi_set_channel";
    }
    snprintf(line, sizeof(line), "PMU1 CMD %s ch=%u filter=%u\n", cmd, channel, filter);
    return _uart_write_line(line);
#else
    pm_cardputer_i2c_wifi_ctrl_t ctrl = {
        .reg = PM_CARDPUTER_REG_WIFI_CTRL,
        .op = op,
        .channel = channel,
        .filter = filter,
    };
    return pm_bsp_i2c_transmit(PM_CARDPUTER_I2C_ADDR,
                               (const uint8_t*)&ctrl,
                               sizeof(ctrl),
                               PM_CARDPUTER_I2C_TIMEOUT_MS);
#endif
}

esp_err_t pm_cardputer_i2c_wifi_promisc_start(uint8_t channel, uint8_t filter) {
    return _wifi_ctrl(PM_CARDPUTER_WIFI_OP_PROMISC_START, channel, filter);
}

esp_err_t pm_cardputer_i2c_wifi_set_channel(uint8_t channel) {
    return _wifi_ctrl(PM_CARDPUTER_WIFI_OP_SET_CHANNEL, channel, 0);
}

esp_err_t pm_cardputer_i2c_wifi_promisc_stop(void) {
    return _wifi_ctrl(PM_CARDPUTER_WIFI_OP_PROMISC_STOP, 0, 0);
}

esp_err_t pm_cardputer_i2c_wifi_frame_pop(pm_cardputer_i2c_wifi_frame_t* out) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    if (!out) return ESP_ERR_INVALID_ARG;
    if (_pop_uart_wifi(out)) return ESP_OK;
    memset(out, 0, sizeof(*out));
    return ESP_OK;
#else
    return _read_reg(PM_CARDPUTER_REG_WIFI_FRAME_POP, out, sizeof(*out));
#endif
}

static esp_err_t _uart_ble_scan_start_raw(bool active) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    char line[96];
    snprintf(line, sizeof(line), "PMU1 CMD ble_scan_start active=%u interval=100 window=80\n",
             active ? 1u : 0u);
    return _uart_write_line(line);
#else
    (void)active;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t _uart_ble_scan_stop_raw(void) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    return _uart_write_line("PMU1 CMD ble_scan_stop\n");
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t pm_cardputer_i2c_ble_scan_start(bool active) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    esp_err_t err = _uart_ble_scan_start_raw(active);
    if (err == ESP_OK) {
        s_uart_ble_scan_requested = true;
        s_uart_ble_scan_active_mode = active;
    }
    return err;
#else
    (void)active;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t pm_cardputer_i2c_ble_scan_stop(void) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    esp_err_t err = _uart_ble_scan_stop_raw();
    if (err == ESP_OK) s_uart_ble_scan_requested = false;
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t pm_cardputer_i2c_ble_seen_pop(pm_cardputer_i2c_ble_seen_t* out) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    if (!out) return ESP_ERR_INVALID_ARG;
    if (_pop_uart_ble(out)) return ESP_OK;
    memset(out, 0, sizeof(*out));
    return ESP_OK;
#else
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static uint32_t _apply_key_shift(uint32_t code, uint8_t modifiers) {
    bool shift = (modifiers & (PM_CARDPUTER_KEY_MOD_LSHIFT |
                               PM_CARDPUTER_KEY_MOD_RSHIFT)) != 0;
    if (!shift) return code;

    if (code >= 'a' && code <= 'z') return code - ('a' - 'A');
    switch (code) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    case '`': return '~';
    default: return code;
    }
}

static void _post_key(const pm_cardputer_i2c_key_t* k) {
    if (!k || !k->available || !k->down) return;

    pm_input_event_t e = {
        .kind = PM_INPUT_KEY,
        .source = PM_INPUT_SRC_I2C_KEYBOARD,
        .code = _apply_key_shift(k->code, k->modifiers),
        .down = true,
        .x = 0,
        .y = 0,
        .timestamp = pm_millis(),
    };
    pm_input_post(&e);
}

static void _mirror_gps(const pm_cardputer_i2c_gps_t* g) {
    if (!g) return;
    bool fresh = (g->age_ms <= 5000);
    if (!g->valid && !fresh) return;

    pm_gps_state_set((double)g->lat_e7 / 10000000.0,
                     (double)g->lon_e7 / 10000000.0,
                     (double)g->alt_cm / 100.0,
                     g->sats,
                     g->valid != 0,
                     0.0);
}

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

static unsigned long _kv_ulong(const char* line, const char* key, unsigned long fallback) {
    const char* v = _kv_value(line, key);
    if (!v) return fallback;
    char* end = NULL;
    unsigned long n = strtoul(v, &end, 0);
    return (end && end != v) ? n : fallback;
}

static int _uart_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t _uart_hex_decode(const char* hex, uint8_t* out, size_t out_len) {
    if (!hex || !out || out_len == 0) return 0;
    size_t n = 0;
    while (hex[0] && hex[1] && n < out_len) {
        if (hex[0] == ' ' || hex[0] == '\r' || hex[0] == '\n') break;
        int hi = _uart_hex_nibble(hex[0]);
        int lo = _uart_hex_nibble(hex[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

static void _uart_hex_encode(const uint8_t* data, size_t len, char* out, size_t out_len) {
    static const char k_hex[] = "0123456789ABCDEF";
    if (!out || out_len == 0) return;
    size_t pos = 0;
    for (size_t i = 0; data && i < len && pos + 2 < out_len; i++) {
        out[pos++] = k_hex[(data[i] >> 4) & 0x0f];
        out[pos++] = k_hex[data[i] & 0x0f];
    }
    out[pos] = 0;
}

static esp_err_t _uart_write_line(const char* line) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    if (!line || !s_uart_started) return ESP_ERR_INVALID_STATE;
    bool locked = false;
    if (s_uart_write_mutex) {
        if (xSemaphoreTake(s_uart_write_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        locked = true;
    }
    int n = uart_write_bytes(PM_CARDPUTER_UART_NUM, line, strlen(line));
    if (locked) xSemaphoreGive(s_uart_write_mutex);
    return n >= 0 ? ESP_OK : ESP_FAIL;
#else
    (void)line;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static uint32_t _required_cap_for_op(const char* op) {
    if (!op) return 0;
    if (strncmp(op, "lora_", 5) == 0) {
        return PM_CARDPUTER_CAP_LORA;
    }
    if (strncmp(op, "wifi_", 5) == 0 ||
        strcmp(op, "promiscuous_start") == 0 ||
        strcmp(op, "promiscuous_stop") == 0 ||
        strcmp(op, "promiscuous_set_channel") == 0) {
        return PM_CARDPUTER_CAP_WIFI_PROMISC;
    }
    if (strncmp(op, "ble_", 4) == 0) {
        return PM_CARDPUTER_CAP_BLE;
    }
    if (strncmp(op, "gps_", 4) == 0) {
        return PM_CARDPUTER_CAP_GPS;
    }
    if (strncmp(op, "key_", 4) == 0) {
        return PM_CARDPUTER_CAP_KEYBOARD;
    }
    return 0;
}

static int _require_uart_link_for_op(const char* op) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    if (!s_uart_seen) {
        pm_log_w(TAG, "Cardputer op '%s' requested before UART HELLO", op ? op : "(null)");
        return -5;
    }
    uint32_t cap = _required_cap_for_op(op);
    if (cap && !(s_caps & cap)) {
        pm_log_w(TAG, "Cardputer op '%s' missing caps=0x%08lx need=0x%08lx",
                 op ? op : "(null)", (unsigned long)s_caps, (unsigned long)cap);
        return -6;
    }
#else
    (void)op;
#endif
    return 0;
}

static void _push_uart_wifi(const pm_cardputer_i2c_wifi_frame_t* f) {
    if (!f || !f->available) return;
    portENTER_CRITICAL(&s_uart_mux);
    if (s_uart_wifi_count < PM_CARDPUTER_UART_WIFI_Q_DEPTH) {
        s_uart_wifi_q[s_uart_wifi_head] = *f;
        s_uart_wifi_head = (uint8_t)((s_uart_wifi_head + 1) % PM_CARDPUTER_UART_WIFI_Q_DEPTH);
        s_uart_wifi_count++;
    }
    portEXIT_CRITICAL(&s_uart_mux);
}

static bool _pop_uart_wifi(pm_cardputer_i2c_wifi_frame_t* out) {
    bool ok = false;
    if (!out) return false;
    portENTER_CRITICAL(&s_uart_mux);
    if (s_uart_wifi_count > 0) {
        *out = s_uart_wifi_q[s_uart_wifi_tail];
        s_uart_wifi_tail = (uint8_t)((s_uart_wifi_tail + 1) % PM_CARDPUTER_UART_WIFI_Q_DEPTH);
        s_uart_wifi_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_uart_mux);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

static void _push_uart_lora(const pm_cardputer_i2c_lora_rx_t* f) {
    if (!f || !f->available) return;
    portENTER_CRITICAL(&s_uart_mux);
    if (s_uart_lora_count < PM_CARDPUTER_UART_LORA_Q_DEPTH) {
        s_uart_lora_q[s_uart_lora_head] = *f;
        s_uart_lora_head = (uint8_t)((s_uart_lora_head + 1) % PM_CARDPUTER_UART_LORA_Q_DEPTH);
        s_uart_lora_count++;
    }
    portEXIT_CRITICAL(&s_uart_mux);
}

static bool _pop_uart_lora(pm_cardputer_i2c_lora_rx_t* out) {
    bool ok = false;
    if (!out) return false;
    portENTER_CRITICAL(&s_uart_mux);
    if (s_uart_lora_count > 0) {
        *out = s_uart_lora_q[s_uart_lora_tail];
        s_uart_lora_tail = (uint8_t)((s_uart_lora_tail + 1) % PM_CARDPUTER_UART_LORA_Q_DEPTH);
        s_uart_lora_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_uart_mux);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

static void _push_uart_ble(const pm_cardputer_i2c_ble_seen_t* b) {
    if (!b || !b->available || !b->mac[0]) return;
    portENTER_CRITICAL(&s_uart_mux);
    if (s_uart_ble_count < PM_CARDPUTER_UART_BLE_Q_DEPTH) {
        s_uart_ble_q[s_uart_ble_head] = *b;
        s_uart_ble_head = (uint8_t)((s_uart_ble_head + 1) % PM_CARDPUTER_UART_BLE_Q_DEPTH);
        s_uart_ble_count++;
    } else {
        s_uart_ble_q[s_uart_ble_tail] = *b;
        s_uart_ble_tail = (uint8_t)((s_uart_ble_tail + 1) % PM_CARDPUTER_UART_BLE_Q_DEPTH);
        s_uart_ble_head = s_uart_ble_tail;
    }
    portEXIT_CRITICAL(&s_uart_mux);
}

static bool _pop_uart_ble(pm_cardputer_i2c_ble_seen_t* out) {
    bool ok = false;
    if (!out) return false;
    portENTER_CRITICAL(&s_uart_mux);
    if (s_uart_ble_count > 0) {
        *out = s_uart_ble_q[s_uart_ble_tail];
        s_uart_ble_tail = (uint8_t)((s_uart_ble_tail + 1) % PM_CARDPUTER_UART_BLE_Q_DEPTH);
        s_uart_ble_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_uart_mux);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

static void _copy_kv_token(const char* line, const char* key, char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = 0;
    const char* v = _kv_value(line, key);
    if (!v) return;
    size_t n = 0;
    while (v[n] && v[n] != ' ' && v[n] != '\r' && v[n] != '\n' && n + 1 < out_len) {
        out[n] = v[n];
        n++;
    }
    out[n] = 0;
}

static void _decode_hex_string(const char* hex, char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = 0;
    if (!hex) return;
    uint8_t tmp[PM_CARDPUTER_I2C_BLE_NAME_MAX - 1];
    size_t got = _uart_hex_decode(hex, tmp, sizeof(tmp));
    for (size_t i = 0; i < got && i + 1 < out_len; i++) {
        char c = (char)tmp[i];
        out[i] = (c >= 32 && c <= 126) ? c : '.';
        out[i + 1] = 0;
    }
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

static void _handle_uart_line(const char* line) {
    if (!line || strncmp(line, "PMU1 ", 5) != 0) return;

    if (strncmp(line + 5, "HELLO", 5) == 0) {
        uint32_t new_caps = (uint32_t)_kv_ulong(line, "caps", _default_caps());
        bool first = !s_uart_seen;
        if (first || new_caps != s_caps) {
            pm_log_i(TAG, "Cardputer UART hello caps=0x%08lx", (unsigned long)new_caps);
        }
        s_caps = new_caps;
        s_uart_seen = true;
        return;
    }

    s_uart_seen = true;

    if (strncmp(line + 5, "GPS", 3) == 0) {
        pm_cardputer_i2c_gps_t g = {
            .valid = (uint8_t)_kv_ulong(line, "valid", 0),
            .sats = (uint8_t)_kv_ulong(line, "sats", 0),
            .fix_quality = (uint8_t)_kv_ulong(line, "fix", 0),
            .lat_e7 = (int32_t)_kv_long(line, "lat_e7", 0),
            .lon_e7 = (int32_t)_kv_long(line, "lon_e7", 0),
            .alt_cm = (int32_t)_kv_long(line, "alt_cm", 0),
            .age_ms = (uint32_t)_kv_ulong(line, "age", 0),
        };
        _mirror_gps(&g);
        return;
    }

    if (strncmp(line + 5, "KEY", 3) == 0) {
        pm_cardputer_i2c_key_t k = {
            .available = 1,
            .kind = 0,
            .down = (uint8_t)_kv_ulong(line, "down", 1),
            .modifiers = (uint8_t)_kv_ulong(line, "mod", 0),
            .code = (uint32_t)_kv_ulong(line, "code", 0),
            .timestamp_ms = pm_millis(),
        };
        _post_key(&k);
        return;
    }

    if (strncmp(line + 5, "WF", 2) == 0) {
        pm_cardputer_i2c_wifi_frame_t f = {
            .available = 1,
            .frame_type = (uint8_t)_kv_ulong(line, "type", 0),
            .channel = (uint8_t)_kv_ulong(line, "ch", 0),
            .rssi = (int8_t)_kv_long(line, "rssi", 0),
            .len = (uint16_t)_kv_ulong(line, "len", 0),
        };
        const char* mac = _kv_value(line, "mac");
        if (mac && strlen(mac) >= 12) {
            for (int i = 0; i < 6; i++) {
                int hi = _uart_hex_nibble(mac[i * 2]);
                int lo = _uart_hex_nibble(mac[i * 2 + 1]);
                f.mac[i] = (hi >= 0 && lo >= 0) ? (uint8_t)((hi << 4) | lo) : 0;
            }
        }
        const char* data = _kv_value(line, "data");
        size_t got = _uart_hex_decode(data, f.data, sizeof(f.data));
        if (f.len == 0 || f.len > got) f.len = (uint16_t)got;
        _push_uart_wifi(&f);
        return;
    }

    if (strncmp(line + 5, "LORA", 4) == 0) {
        pm_cardputer_i2c_lora_rx_t rx = {
            .available = 1,
            .rssi = (int8_t)_kv_long(line, "rssi", 0),
            .snr_x4 = (int8_t)_kv_long(line, "snr_x4", 0),
            .freq_khz = (uint32_t)_kv_ulong(line, "freq", 0),
        };
        const char* data = _kv_value(line, "data");
        size_t got = _uart_hex_decode(data, rx.data, sizeof(rx.data));
        rx.len = (uint8_t)got;
        _push_uart_lora(&rx);
        return;
    }

    if (strncmp(line + 5, "BLE", 3) == 0) {
        pm_cardputer_i2c_ble_seen_t b = {
            .available = 1,
            .rssi = (int8_t)_kv_long(line, "rssi", 0),
        };
        char raw_mac[24] = {0};
        _copy_kv_token(line, "mac", raw_mac, sizeof(raw_mac));
        _normalize_mac(raw_mac, b.mac, sizeof(b.mac));
        _copy_kv_token(line, "type", b.addr_type, sizeof(b.addr_type));
        if (!b.addr_type[0]) snprintf(b.addr_type, sizeof(b.addr_type), "remote");
        _copy_kv_token(line, "mfg", b.mfg, sizeof(b.mfg));

        char name_hex[PM_CARDPUTER_I2C_BLE_NAME_MAX * 2] = {0};
        _copy_kv_token(line, "name_hex", name_hex, sizeof(name_hex));
        if (name_hex[0]) {
            _decode_hex_string(name_hex, b.name, sizeof(b.name));
        } else {
            _copy_kv_token(line, "name", b.name, sizeof(b.name));
        }

        if (b.mac[0]) _push_uart_ble(&b);
        return;
    }
}

static void _uart_rx_task(void* arg) {
    (void)arg;
    char line[PM_CARDPUTER_UART_LINE_MAX];
    size_t pos = 0;
    uint32_t last_ping = 0;
    uint32_t last_diag = 0;
    uint32_t last_diag_bytes = 0;

    while (s_uart_started) {
        uint8_t ch = 0;
        int n = uart_read_bytes(PM_CARDPUTER_UART_NUM, &ch, 1, pdMS_TO_TICKS(20));
        if (n > 0) {
            s_uart_rx_bytes += (uint32_t)n;
            if (ch == '\n') {
                line[pos] = 0;
                if (pos > 0 && line[pos - 1] == '\r') line[pos - 1] = 0;
                _handle_uart_line(line);
                pos = 0;
            } else if (pos + 1 < sizeof(line)) {
                line[pos++] = (char)ch;
            } else {
                pos = 0;
            }
        }

        uint32_t now = pm_millis();
        if (now - last_ping > 1000) {
            _uart_write_line("PMU1 PING\n");
            last_ping = now;
        }
        if (!s_uart_seen && now - last_diag > 5000) {
            pm_log_w(TAG, "Cardputer UART waiting for HELLO: rx_bytes=%lu (+%lu) RX=IO%d TX=IO%d baud=%d",
                     (unsigned long)s_uart_rx_bytes,
                     (unsigned long)(s_uart_rx_bytes - last_diag_bytes),
                     PM_CARDPUTER_UART_RX_PIN,
                     PM_CARDPUTER_UART_TX_PIN,
                     PM_CARDPUTER_UART_BAUD);
            last_diag_bytes = s_uart_rx_bytes;
            last_diag = now;
        }
    }

    s_uart_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t _uart_bridge_start(void) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
    if (s_uart_started) return ESP_OK;

    uart_config_t cfg = {
        .baud_rate = PM_CARDPUTER_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(PM_CARDPUTER_UART_NUM, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(PM_CARDPUTER_UART_NUM,
                       PM_CARDPUTER_UART_TX_PIN,
                       PM_CARDPUTER_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    err = uart_driver_install(PM_CARDPUTER_UART_NUM,
                              PM_CARDPUTER_UART_RX_BYTES,
                              PM_CARDPUTER_UART_TX_BYTES,
                              0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    if (!s_uart_write_mutex) {
        s_uart_write_mutex = xSemaphoreCreateMutex();
        if (!s_uart_write_mutex) return ESP_ERR_NO_MEM;
    }
    s_uart_started = true;
    if (!s_caps) s_caps = _default_caps();
    snprintf(s_name, sizeof(s_name), "Cardputer ADV UART");
    xTaskCreatePinnedToCore(_uart_rx_task, "pm_cardputer_uart",
                            PM_CARDPUTER_UART_TASK_STACK,
                            NULL, 4, &s_uart_task, 0);
    pm_log_i(TAG, "Cardputer UART1 bridge armed: RX=IO%d TX=IO%d baud=%d",
             PM_CARDPUTER_UART_RX_PIN,
             PM_CARDPUTER_UART_TX_PIN,
             PM_CARDPUTER_UART_BAUD);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void _gps_task(void* arg) {
    (void)arg;
    while (s_present) {
        pm_cardputer_i2c_gps_t g = {0};
        esp_err_t err = pm_cardputer_i2c_read_gps(&g);
        if (err == ESP_OK) {
            _mirror_gps(&g);
        }
        vTaskDelay(pdMS_TO_TICKS(PM_CARDPUTER_GPS_POLL_MS));
    }
    s_gps_task = NULL;
    vTaskDelete(NULL);
}

static void __attribute__((unused)) _start_gps_task(void) {
    if (s_gps_task || !(s_caps & PM_CARDPUTER_CAP_GPS)) return;
    xTaskCreatePinnedToCore(_gps_task, "pm_cardputer_gps", 4096,
                            NULL, 3, &s_gps_task, 0);
}

static void _key_task(void* arg) {
    (void)arg;
    while (s_present) {
        pm_cardputer_i2c_key_t k = {0};
        esp_err_t err = pm_cardputer_i2c_poll_key(&k);
        if (err == ESP_OK) {
            _post_key(&k);
        }
        vTaskDelay(pdMS_TO_TICKS(PM_CARDPUTER_KEY_POLL_MS));
    }
    s_key_task = NULL;
    vTaskDelete(NULL);
}

static void __attribute__((unused)) _start_key_task(void) {
    if (s_key_task || !(s_caps & PM_CARDPUTER_CAP_KEYBOARD)) return;
    xTaskCreatePinnedToCore(_key_task, "pm_cardputer_kb", 4096,
                            NULL, 4, &s_key_task, 0);
}

bool pm_cardputer_i2c_init_auto(void) {
    if (s_present) return true;

#if PM_BOARD_CARDPUTER_UART_BRIDGE
    esp_err_t uart_err = _uart_bridge_start();
    if (uart_err != ESP_OK) {
        pm_log_w(TAG, "Cardputer UART bridge start failed: %s", esp_err_to_name(uart_err));
        return false;
    }
    s_present = true;
    s_caps = _default_caps();
    pm_log_i(TAG, "Cardputer UART module armed; waiting for HELLO on UART1");
    return true;
#else
    pm_cardputer_i2c_whoami_t who = {0};
    esp_err_t err = pm_cardputer_i2c_read_whoami(&who);
    if (err != ESP_OK) {
        pm_log_i(TAG, "Cardputer I2C module not present");
        return false;
    }

    s_present = true;
    s_caps = who.caps;
    memcpy(s_name, who.name, PM_CARDPUTER_I2C_NAME_LEN);
    s_name[PM_CARDPUTER_I2C_NAME_LEN] = 0;
    if (s_name[0] == 0) {
        snprintf(s_name, sizeof(s_name), "Cardputer ADV");
    }

    pm_log_i(TAG, "Cardputer I2C module present: %s caps=0x%08lx",
             s_name, (unsigned long)s_caps);
    _start_key_task();
    _start_gps_task();
    return true;
#endif
}

static int _hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int _extract_hex_payload(const char* params, uint8_t* out, size_t out_len) {
    if (!params || !out || out_len == 0) return -1;

    const char* p = strstr(params, "\"data\"");
    if (!p) p = strstr(params, "data");
    if (!p) p = params;

    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p && (*p == ' ' || *p == '\"' || *p == '\'')) p++;

    size_t n = 0;
    while (p[0] && p[1] && n < out_len) {
        if (p[0] == '\"' || p[0] == '\'' || p[0] == '}' || p[0] == ',') break;
        int hi = _hex_nibble(p[0]);
        int lo = _hex_nibble(p[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        p += 2;
        while (*p == ' ' || *p == ':' || *p == '-') p++;
    }
    return (int)n;
}

static int _extract_uint_param(const char* params, const char* key, int fallback) {
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

static const char* _wifi_frame_type(uint8_t type) {
    switch (type) {
        case 1: return "mgmt";
        case 2: return "data";
        case 3: return "ctrl";
        default: return "unk";
    }
}

int pm_cardputer_i2c_call(const char* op, const char* params) {
    if (!op || !s_present) return -1;

    if (strcmp(op, "ping") == 0) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
        return _uart_write_line("PMU1 PING\n") == ESP_OK ? 0 : -2;
#else
        uint8_t pong = 0;
        esp_err_t err = _read_reg(PM_CARDPUTER_REG_PING, &pong, sizeof(pong));
        return (err == ESP_OK && pong == 0xa5) ? 0 : -2;
#endif
    }

    if (strcmp(op, "status") == 0) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
        pm_log_i(TAG, "status uart_started=%d uart_seen=%d caps=0x%08lx wifi_q=%u lora_q=%u",
                 s_uart_started, s_uart_seen, (unsigned long)s_caps,
                 s_uart_wifi_count, s_uart_lora_count);
        return 0;
#else
        pm_cardputer_i2c_status_t st = {0};
        esp_err_t err = pm_cardputer_i2c_read_status(&st);
        if (err != ESP_OK) return -2;
        pm_log_i(TAG, "status uptime=%lu heap=%ld queued_keys=%u queued_lora=%u",
                 (unsigned long)st.uptime_ms,
                 (long)st.heap_free,
                 st.queued_keys,
                 st.queued_lora);
        return 0;
#endif
    }

    int ready = _require_uart_link_for_op(op);
    if (ready != 0) return ready;

    if (strcmp(op, "gps_get") == 0) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
        pm_log_i(TAG, "gps_get: UART bridge pushes GPS into pm_gps_state");
        return 0;
#else
        pm_cardputer_i2c_gps_t g = {0};
        esp_err_t err = pm_cardputer_i2c_read_gps(&g);
        if (err != ESP_OK) return -2;
        pm_log_i(TAG, "gps valid=%u sats=%u lat_e7=%ld lon_e7=%ld age=%lu",
                 g.valid, g.sats, (long)g.lat_e7, (long)g.lon_e7,
                 (unsigned long)g.age_ms);
        return 0;
#endif
    }

    if (strcmp(op, "key_poll") == 0) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
        return 1;
#else
        pm_cardputer_i2c_key_t k = {0};
        esp_err_t err = pm_cardputer_i2c_poll_key(&k);
        if (err != ESP_OK) return -2;
        _post_key(&k);
        return k.available ? 0 : 1;
#endif
    }

    if (strcmp(op, "lora_tx") == 0) {
        uint8_t buf[PM_CARDPUTER_I2C_LORA_MAX];
        int len = _extract_hex_payload(params, buf, sizeof(buf));
        if (len <= 0) return -3;
        esp_err_t err = pm_cardputer_i2c_lora_tx(buf, (uint8_t)len);
        return err == ESP_OK ? 0 : -2;
    }

    if (strcmp(op, "lora_mesh_start") == 0 ||
        strcmp(op, "lora_rx_start") == 0 ||
        strcmp(op, "lora_start") == 0) {
        int channel = _extract_uint_param(params, "channel", 0);
        if (channel < 0) channel = 0;
        if (channel > 7) channel = 7;
        uint32_t freq_khz = 906875u + ((uint32_t)channel * 3125u);
#if PM_BOARD_CARDPUTER_UART_BRIDGE
        char line[96];
        snprintf(line, sizeof(line),
                 "PMU1 CMD lora_mesh_start ch=%u freq=%lu\n",
                 (unsigned)channel, (unsigned long)freq_khz);
        esp_err_t err = _uart_write_line(line);
        if (err == ESP_OK) {
            pm_log_i(TAG, "Cardputer LoRa mesh start ch=%u freq=%lu kHz",
                     (unsigned)channel, (unsigned long)freq_khz);
        }
        return err == ESP_OK ? 0 : -2;
#else
        pm_log_i(TAG, "Cardputer I2C LoRa mesh start ch=%u freq=%lu kHz",
                 (unsigned)channel, (unsigned long)freq_khz);
        return 0;
#endif
    }

    if (strcmp(op, "lora_stop") == 0 ||
        strcmp(op, "lora_mesh_stop") == 0) {
#if PM_BOARD_CARDPUTER_UART_BRIDGE
        esp_err_t err = _uart_write_line("PMU1 CMD lora_stop\n");
        if (err == ESP_OK) pm_log_i(TAG, "Cardputer LoRa stop");
        return err == ESP_OK ? 0 : -2;
#else
        return 0;
#endif
    }

    if (strcmp(op, "lora_rx_pop") == 0) {
        pm_cardputer_i2c_lora_rx_t rx = {0};
        esp_err_t err = pm_cardputer_i2c_lora_rx_pop(&rx);
        if (err != ESP_OK) return -2;
        return rx.available ? 0 : 1;
    }

    if (strcmp(op, "wifi_promisc_start") == 0 ||
        strcmp(op, "promiscuous_start") == 0) {
        uint8_t channel = (uint8_t)_extract_uint_param(params, "channel", 0);
        uint8_t filter = (uint8_t)_extract_uint_param(params, "filter", 0xff);
        esp_err_t err = pm_cardputer_i2c_wifi_promisc_start(channel, filter);
        if (err == ESP_OK) {
            pm_log_i(TAG, "Cardputer WiFi promiscuous start ch=%u filter=0x%02x",
                     channel, filter);
        }
        return err == ESP_OK ? 0 : -2;
    }

    if (strcmp(op, "wifi_promisc_stop") == 0 ||
        strcmp(op, "promiscuous_stop") == 0) {
        esp_err_t err = pm_cardputer_i2c_wifi_promisc_stop();
        if (err == ESP_OK) pm_log_i(TAG, "Cardputer WiFi promiscuous stop");
        return err == ESP_OK ? 0 : -2;
    }

    if (strcmp(op, "wifi_set_channel") == 0 ||
        strcmp(op, "promiscuous_set_channel") == 0) {
        uint8_t channel = (uint8_t)_extract_uint_param(params, "channel", 1);
        esp_err_t err = pm_cardputer_i2c_wifi_set_channel(channel);
        if (err == ESP_OK) {
            pm_log_d(TAG, "Cardputer WiFi promiscuous channel=%u", channel);
        }
        return err == ESP_OK ? 0 : -2;
    }

    if (strcmp(op, "wifi_frame_pop") == 0) {
        pm_cardputer_i2c_wifi_frame_t f = {0};
        esp_err_t err = pm_cardputer_i2c_wifi_frame_pop(&f);
        if (err != ESP_OK) return -2;
        if (!f.available) return 1;
        pm_log_d(TAG, "wifi frame %s %02x:%02x:%02x:%02x:%02x:%02x rssi=%d ch=%u",
                 _wifi_frame_type(f.frame_type),
                 f.mac[0], f.mac[1], f.mac[2], f.mac[3], f.mac[4], f.mac[5],
                 f.rssi, f.channel);
        return 0;
    }

    if (strcmp(op, "ble_scan_start") == 0 ||
        strcmp(op, "ble_start") == 0) {
        bool active = _extract_uint_param(params, "active", 0) != 0;
        esp_err_t err = pm_cardputer_i2c_ble_scan_start(active);
        if (err == ESP_OK) {
            pm_log_i(TAG, "Cardputer BLE scan start active=%d", active ? 1 : 0);
        }
        return err == ESP_OK ? 0 : -2;
    }

    if (strcmp(op, "ble_scan_stop") == 0 ||
        strcmp(op, "ble_stop") == 0) {
        esp_err_t err = pm_cardputer_i2c_ble_scan_stop();
        if (err == ESP_OK) pm_log_i(TAG, "Cardputer BLE scan stop");
        return err == ESP_OK ? 0 : -2;
    }

    if (strcmp(op, "ble_seen_pop") == 0) {
        pm_cardputer_i2c_ble_seen_t b = {0};
        esp_err_t err = pm_cardputer_i2c_ble_seen_pop(&b);
        if (err != ESP_OK) return -2;
        if (!b.available) return 1;
        pm_log_d(TAG, "ble seen %s rssi=%d name=%s", b.mac, b.rssi, b.name);
        return 0;
    }

    pm_log_w(TAG, "unknown Cardputer op '%s'", op);
    return -4;
}
