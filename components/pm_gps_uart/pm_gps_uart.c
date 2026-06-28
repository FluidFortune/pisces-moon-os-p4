// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_gps_uart.c — NMEA parser for P4-direct GPS
//
//  Reads UART bytes, accumulates one sentence at a time,
//  validates checksum, dispatches by talker+sentence.
//
//  Sentences we care about (BN-180 default output @ 9600):
//    $GPRMC — recommended minimum (lat/lng/speed/valid)
//    $GPGGA — fix data (alt/sats)
//    $GPVTG — track/speed (we use RMC's speed instead)
//    $GPGSA — DOP and active sats (could derive HDOP later)
//
//  Both sentences feed the same pm_gps_state_set() call. We
//  cache the most recent value of each field as it arrives,
//  then push a unified snapshot whenever we get a new RMC
//  (since RMC has the validity flag).
//
//  Checksum: NMEA-0183 standard XOR of all bytes between
//  '$' and '*', exclusive. Hex format after '*'.
// ============================================================

#include "pm_gps_uart.h"
#include "pm_gps_state.h"
#include "pm_hal.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

static const char* TAG = "PM_GPS_UART";

#define UART_RX_BUF_BYTES   2048
#define MAX_SENTENCE_LEN    192     // NMEA-0183 says 82; some modules run long
#define GPS_DIAG_SWEEP      1       // temporary hardware bring-up aid
#define GPS_DIAG_SAMPLE_MS  1300
#define GPS_DIAG_MAX_BYTES  256

// ── Stats ────────────────────────────────────────────────────
static pm_gps_uart_stats_t s_stats;
static uint32_t s_pin_probe_started_ms = 0;
static uint32_t s_last_stats_log_ms = 0;
static uint32_t s_next_preview_at = 1;
static char s_preview_ascii[65];
static char s_preview_hex[3 * 64 + 1];
static uint8_t s_preview_len = 0;

static const uint32_t s_diag_bauds[] = {
    9600, 38400, 115200, 57600, 4800, 19200, 230400
};

// ── Latest field cache (filled across multiple sentences) ────
typedef struct {
    bool   has_position;
    double lat;
    double lng;
    bool   has_alt;
    double alt_m;
    bool   has_sats;
    int    sats;
    bool   has_speed;
    double speed_mps;
    bool   valid_flag_set;     // set by RMC; A=valid, V=invalid
    bool   valid_flag;
} gps_fields_t;

static gps_fields_t s_fields;

// ─────────────────────────────────────────────
//  Field-cache push to global state
//
//  We push whenever we get a fresh RMC (which carries the
//  validity flag). Other sentences just update the cache.
// ─────────────────────────────────────────────
static void _push_state(void) {
    if (!s_fields.has_position && !s_fields.has_sats &&
        !s_fields.valid_flag_set && !s_fields.has_alt) {
        return;
    }
    pm_gps_state_set(s_fields.has_position ? s_fields.lat : 0.0,
                      s_fields.has_position ? s_fields.lng : 0.0,
                      s_fields.has_alt ? s_fields.alt_m : 0.0,
                      s_fields.has_sats ? s_fields.sats : 0,
                      s_fields.valid_flag_set ? s_fields.valid_flag : false,
                      s_fields.has_speed ? s_fields.speed_mps : 0.0);
}

// ─────────────────────────────────────────────
//  NMEA helpers
// ─────────────────────────────────────────────
static int _hex_nybble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

// Verify checksum. Sentence form: "$GPRMC,...,...*HH"
// Checksum XORs bytes between '$' and '*', exclusive.
static bool _verify_checksum(const char* line, int len) {
    if (len < 4 || line[0] != '$') return false;
    int star = -1;
    for (int i = 0; i < len; i++) {
        if (line[i] == '*') { star = i; break; }
    }
    if (star < 0 || star + 3 > len) return false;
    uint8_t cs = 0;
    for (int i = 1; i < star; i++) cs ^= (uint8_t)line[i];
    int hi = _hex_nybble(line[star + 1]);
    int lo = _hex_nybble(line[star + 2]);
    if (hi < 0 || lo < 0) return false;
    return cs == (uint8_t)((hi << 4) | lo);
}

// Parse "DDMM.mmmm" + N/S/E/W → decimal degrees.
static bool _parse_lat_lng(const char* val, char hemi, bool is_lat,
                            double* out) {
    if (!val || !*val) return false;
    double v = atof(val);
    int    deg_digits = is_lat ? 2 : 3;
    double deg = (int)(v / 100.0);
    double min = v - deg * 100.0;
    double dd  = deg + (min / 60.0);
    if (hemi == 'S' || hemi == 'W') dd = -dd;
    (void)deg_digits;
    *out = dd;
    return true;
}

// Knots → m/s. NMEA RMC reports speed in knots.
static double _knots_to_mps(double knots) { return knots * 0.514444; }

// Walk comma-separated fields. dst[i] = pointer to field i (in
// place; we write NULs over commas). Returns field count.
static int _split_csv(char* line, char* dst[], int max_fields) {
    int n = 0;
    char* p = line;
    while (n < max_fields) {
        dst[n++] = p;
        char* c = strchr(p, ',');
        if (!c) break;
        *c = 0;
        p  = c + 1;
    }
    return n;
}

// ─────────────────────────────────────────────
//  Sentence handlers
// ─────────────────────────────────────────────
//
//  $GPRMC,hhmmss,A,lat,N,lng,W,knots,track,ddmmyy,mag,E*hh
//   field 0: $GPRMC
//   field 1: time
//   field 2: status (A=valid, V=invalid)
//   field 3: lat
//   field 4: N/S
//   field 5: lng
//   field 6: E/W
//   field 7: speed knots
//   field 8: track angle
//   field 9: date
static void _handle_rmc(char* fields[], int nf) {
    if (nf < 10) return;
    s_fields.valid_flag_set = true;
    s_fields.valid_flag = (fields[2][0] == 'A');

    double lat = 0, lng = 0;
    if (_parse_lat_lng(fields[3], fields[4][0], true,  &lat) &&
        _parse_lat_lng(fields[5], fields[6][0], false, &lng)) {
        s_fields.lat = lat;
        s_fields.lng = lng;
        s_fields.has_position = true;
    }

    if (fields[7][0]) {
        s_fields.speed_mps = _knots_to_mps(atof(fields[7]));
        s_fields.has_speed = true;
    }

    if (s_fields.valid_flag) s_stats.fixes_valid++;
    else                       s_stats.fixes_invalid++;

    // Push a unified snapshot now.
    _push_state();
}

//
//  $GPGGA,hhmmss,lat,N,lng,W,quality,sats,hdop,alt,M,...
//   field 6: quality (0=no fix, 1=GPS, 2=DGPS, ...)
//   field 7: number of sats
//   field 8: HDOP
//   field 9: altitude (m)
static void _handle_gga(char* fields[], int nf) {
    if (nf < 10) return;
    if (fields[6][0]) {
        s_fields.valid_flag_set = true;
        s_fields.valid_flag = atoi(fields[6]) > 0;
    }
    if (fields[7][0]) {
        s_fields.sats     = atoi(fields[7]);
        s_fields.has_sats = true;
    }
    if (fields[9][0]) {
        s_fields.alt_m   = atof(fields[9]);
        s_fields.has_alt = true;
    }
    // GGA also carries position; if RMC hasn't filled it yet, do it here.
    if (!s_fields.has_position) {
        double lat = 0, lng = 0;
        if (_parse_lat_lng(fields[2], fields[3][0], true,  &lat) &&
            _parse_lat_lng(fields[4], fields[5][0], false, &lng)) {
            s_fields.lat = lat;
            s_fields.lng = lng;
            s_fields.has_position = true;
        }
    }
    _push_state();
}

// ─────────────────────────────────────────────
//  Sentence dispatch
// ─────────────────────────────────────────────
static void _process_sentence(char* line, int len) {
    if (!_verify_checksum(line, len)) {
        s_stats.sentences_bad++;
        return;
    }
    s_stats.sentences_seen++;

    // Strip trailing *HH so split_csv doesn't see it.
    char* star = strchr(line, '*');
    if (star) *star = 0;

    char* fields[24];
    int nf = _split_csv(line, fields, 24);
    if (nf < 1) return;

    // talker = fields[0][1..2], sentence = fields[0][3..5]
    const char* tag = fields[0];
    if (strlen(tag) < 6) return;
    if (strncmp(tag + 3, "RMC", 3) == 0) _handle_rmc(fields, nf);
    else if (strncmp(tag + 3, "GGA", 3) == 0) _handle_gga(fields, nf);
    // Other sentences quietly ignored.
}

// ─────────────────────────────────────────────
//  UART read task
// ─────────────────────────────────────────────
static char  s_line[MAX_SENTENCE_LEN];
static int   s_line_len = 0;

static esp_err_t _gps_apply_baud(uint32_t baud) {
    esp_err_t err = uart_set_baudrate(PM_GPS_UART_NUM, baud);
    if (err != ESP_OK) {
        pm_log_w(TAG, "GPS UART baud %u failed: %s",
                 (unsigned)baud, esp_err_to_name(err));
        return err;
    }

    s_stats.active_baud = baud;
    s_line_len = 0;
    s_preview_len = 0;
    s_next_preview_at = s_stats.bytes_rx + 1;
    uart_flush_input(PM_GPS_UART_NUM);
    pm_log_i(TAG, "GPS UART baud: %u", (unsigned)baud);
    return ESP_OK;
}

static bool _gps_sample_has_nmea(const uint8_t* buf, int n) {
    if (!buf || n < 4) return false;
    for (int i = 0; i <= n - 4; i++) {
        if (buf[i] != '$') continue;
        if ((buf[i + 1] == 'G' || buf[i + 1] == 'P') &&
            (buf[i + 2] == 'P' || buf[i + 2] == 'N' ||
             buf[i + 2] == 'A' || buf[i + 2] == 'L' ||
             buf[i + 2] == 'B')) {
            return true;
        }
    }
    return false;
}

static bool _gps_sample_has_ubx(const uint8_t* buf, int n) {
    if (!buf || n < 2) return false;
    for (int i = 0; i <= n - 2; i++) {
        if (buf[i] == 0xB5 && buf[i + 1] == 0x62) return true;
    }
    return false;
}

static int _gps_sample_printable_count(const uint8_t* buf, int n) {
    int printable = 0;
    for (int i = 0; i < n; i++) {
        if ((buf[i] >= 32 && buf[i] <= 126) ||
            buf[i] == '\r' || buf[i] == '\n') {
            printable++;
        }
    }
    return printable;
}

static void _gps_log_diag_sample(uint32_t baud, const uint8_t* buf, int n) {
    char ascii[65];
    char hex[3 * 64 + 1];
    int shown = n < 64 ? n : 64;

    for (int i = 0; i < shown; i++) {
        uint8_t b = buf[i];
        ascii[i] = (b >= 32 && b <= 126) ? (char)b : '.';
        snprintf(&hex[i * 3], 4, "%02X ", b);
    }
    ascii[shown] = 0;
    hex[shown * 3] = 0;

    int printable = _gps_sample_printable_count(buf, n);
    pm_log_i(TAG,
             "GPS diag baud=%u bytes=%d printable=%d nmea=%s ubx=%s",
             (unsigned)baud, n, printable,
             _gps_sample_has_nmea(buf, n) ? "yes" : "no",
             _gps_sample_has_ubx(buf, n) ? "yes" : "no");
    if (shown > 0) {
        pm_log_i(TAG, "GPS diag baud=%u ascii='%s'", (unsigned)baud, ascii);
        pm_log_i(TAG, "GPS diag baud=%u hex=%s", (unsigned)baud, hex);
    }
}

static int _gps_capture_diag_sample(uint8_t* out, int max_len,
                                    uint32_t window_ms) {
    int total = 0;
    uint32_t start = pm_millis();
    while ((pm_millis() - start) < window_ms && total < max_len) {
        int n = uart_read_bytes(PM_GPS_UART_NUM, out + total,
                                max_len - total, pdMS_TO_TICKS(100));
        if (n > 0) total += n;
    }
    return total;
}

static uint32_t _gps_run_diag_sweep(uint32_t fallback_baud) {
#if GPS_DIAG_SWEEP
    uint8_t sample[GPS_DIAG_MAX_BYTES];
    uint32_t best_baud = fallback_baud;
    int best_printable = -1;

    pm_log_i(TAG, "GPS diag sweep begin on IO%d", PM_GPS_PIN_RX);
    for (size_t i = 0; i < sizeof(s_diag_bauds) / sizeof(s_diag_bauds[0]); i++) {
        uint32_t baud = s_diag_bauds[i];
        if (_gps_apply_baud(baud) != ESP_OK) continue;
        pm_delay_ms(120);
        uart_flush_input(PM_GPS_UART_NUM);

        int n = _gps_capture_diag_sample(sample, sizeof(sample),
                                         GPS_DIAG_SAMPLE_MS);
        _gps_log_diag_sample(baud, sample, n);

        if (_gps_sample_has_nmea(sample, n)) {
            pm_log_i(TAG, "GPS diag selected %u baud: NMEA detected",
                     (unsigned)baud);
            return baud;
        }
        if (_gps_sample_has_ubx(sample, n)) {
            pm_log_w(TAG,
                     "GPS diag saw UBX binary at %u baud; module may not be outputting NMEA",
                     (unsigned)baud);
            best_baud = baud;
        } else {
            int printable = _gps_sample_printable_count(sample, n);
            if (printable > best_printable) {
                best_printable = printable;
                best_baud = baud;
            }
        }
    }

    pm_log_w(TAG,
             "GPS diag found no NMEA marker; settling on %u baud for live raw preview",
             (unsigned)best_baud);
    return best_baud;
#else
    return fallback_baud;
#endif
}

static void _gps_preview_bytes(const uint8_t* buf, int n) {
    if (!buf || n <= 0) return;
    if (s_stats.bytes_rx < s_next_preview_at && s_preview_len == 0) return;

    for (int i = 0; i < n && s_preview_len < 64; i++) {
        uint8_t b = buf[i];
        s_preview_ascii[s_preview_len] =
            (b >= 32 && b <= 126) ? (char)b : '.';
        snprintf(&s_preview_hex[s_preview_len * 3], 4, "%02X ", b);
        s_preview_len++;
    }

    if (s_preview_len >= 64) {
        s_preview_ascii[64] = 0;
        s_preview_hex[64 * 3] = 0;
        pm_log_i(TAG, "GPS raw preview ascii='%s'", s_preview_ascii);
        pm_log_i(TAG, "GPS raw preview hex=%s", s_preview_hex);
        s_preview_len = 0;
        s_next_preview_at = s_stats.bytes_rx + 480;
    }
}

static esp_err_t _gps_apply_pin_route(bool swapped) {
    const int rx = PM_GPS_PIN_RX;
    const int tx = PM_GPS_PIN_TX;
    (void)swapped;
    // GPS is receive-only on the Elecrow bench rig (PM_GPS_PIN_TX = -1
    // → UART_PIN_NO_CHANGE). On LilyGO the L76K accepts $PMTK commands
    // so we wire TX too.
    esp_err_t err = uart_set_pin(PM_GPS_UART_NUM,
                                 tx >= 0 ? tx : UART_PIN_NO_CHANGE,
                                 rx,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) {
        s_stats.active_rx_pin = (uint8_t)rx;
        s_stats.using_swapped_pins = false;
        s_line_len = 0;
        s_preview_len = 0;
        s_next_preview_at = s_stats.bytes_rx + 1;
        uart_flush_input(PM_GPS_UART_NUM);
        if (tx >= 0) {
            pm_log_i(TAG, "GPS UART route: RX=IO%d TX=IO%d", rx, tx);
        } else {
            pm_log_i(TAG, "GPS UART route: RX=IO%d TX=disabled", rx);
        }
    } else {
        pm_log_w(TAG, "GPS UART route failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void _gps_maybe_probe_pins(void) {
    uint32_t now = pm_millis();
    if (s_last_stats_log_ms == 0 || now - s_last_stats_log_ms >= 5000) {
        s_last_stats_log_ms = now;
        pm_log_i(TAG,
                 "GPS stats: RX=IO%d baud=%u bytes=%u good=%u bad=%u valid=%u invalid=%u",
                 (int)s_stats.active_rx_pin,
                 (unsigned)s_stats.active_baud,
                 (unsigned)s_stats.bytes_rx,
                 (unsigned)s_stats.sentences_seen,
                 (unsigned)s_stats.sentences_bad,
                 (unsigned)s_stats.fixes_valid,
                 (unsigned)s_stats.fixes_invalid);
    }

    if (s_stats.bytes_rx != 0) return;
    if (s_pin_probe_started_ms == 0) {
        s_pin_probe_started_ms = now;
        return;
    }

    uint32_t elapsed = now - s_pin_probe_started_ms;
    if (elapsed >= 10000) {
        pm_log_w(TAG, "No GPS bytes on IO%d after 10s; single-pin IO52 mode. Check GPS TX lead, VCC, and GND.",
                 PM_GPS_PIN_RX);
        s_pin_probe_started_ms = now;
    }
}

static void _gps_task(void* arg) {
    (void)arg;
    uint8_t buf[256];
    pm_log_i(TAG, "GPS UART task running on UART%d (RX=IO%d, %d baud)",
             PM_GPS_UART_NUM, PM_GPS_PIN_RX, PM_GPS_BAUD);
    while (true) {
        int n = uart_read_bytes(PM_GPS_UART_NUM, buf, sizeof(buf),
                                  pdMS_TO_TICKS(50));
        if (n <= 0) {
            _gps_maybe_probe_pins();
            continue;
        }
        s_stats.bytes_rx += n;
        _gps_preview_bytes(buf, n);
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                if (s_line_len > 0) {
                    s_line[s_line_len] = 0;
                    _process_sentence(s_line, s_line_len);
                }
                s_line_len = 0;
                continue;
            }
            if (s_line_len >= MAX_SENTENCE_LEN - 1) {
                s_stats.sentences_bad++;
                s_line_len = 0;       // overflow; resync on next \n
                continue;
            }
            s_line[s_line_len++] = c;
        }
    }
}

// ─────────────────────────────────────────────
//  Public init
// ─────────────────────────────────────────────
esp_err_t pm_gps_uart_init(void) {
    uart_config_t cfg = {
        .baud_rate  = PM_GPS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err;
    err = uart_param_config(PM_GPS_UART_NUM, &cfg);
    if (err != ESP_OK) return err;
    err = uart_driver_install(PM_GPS_UART_NUM,
                                UART_RX_BUF_BYTES, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = _gps_apply_pin_route(false);
    if (err != ESP_OK) return err;
    uint32_t live_baud = _gps_run_diag_sweep(PM_GPS_BAUD);
    err = _gps_apply_baud(live_baud);
    if (err != ESP_OK) return err;

    xTaskCreatePinnedToCore(_gps_task, "pm_gps", 4096, NULL, 4, NULL, 0);
    pm_log_i(TAG, "GPS UART initialized");
    return ESP_OK;
}

void pm_gps_uart_stats(pm_gps_uart_stats_t* out) {
    if (!out) return;
    *out = s_stats;
}

void pm_gps_uart_send_cmd(const char* cmd) {
    if (!cmd) return;
    uart_write_bytes(PM_GPS_UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(PM_GPS_UART_NUM, "\r\n", 2);
}
