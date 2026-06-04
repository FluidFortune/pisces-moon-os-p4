// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_mesh_messenger.c — Meshtastic-compatible messenger
//
//  Logic ported from S3 mesh_messenger.cpp. Radio backends:
//    - Local SX1262 via pm_lora (when wireless slot has one)
//    - Cardputer ADV LoRa via pm_cardputer_i2c bridge (fallback)
//
//  Layout (Phase 16, 1024x600 / 800x480):
//    titlebar  : back, "MESH MESSENGER", radio chip, NodeID chip
//    channel   : 8 tab pills across top
//    main row  : message log (left) + heard-nodes panel (right)
//    input     : text field + SEND + freq label
//
//  Per-channel scrollback held in PSRAM. Seen-IDs ring buffer
//  prevents echoing our own packets.
// ============================================================

#include "pm_app_mesh_messenger.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_board.h"
#include "pm_gps_state.h"
#include "pm_lora.h"
#include "pm_radio.h"
#include "pm_peer.h"
#include "pm_input.h"
#include "pm_cardputer_i2c.h"
#include "pm_launcher.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_MESH";

// LongFast US default
#define LORA_FREQ_BASE_MHZ   906.875f
#define MESH_BROADCAST       0xFFFFFFFFu
#define MESH_HOP_LIMIT       3

#define NUM_CHANNELS         8
#define MAX_MSGS_PER_CHANNEL 64
#define SENDER_LEN           24
#define TEXT_LEN             200
#define SEEN_IDS_SIZE        128
#define CARDPUTER_LORA_UNAVAILABLE_TEXT "Cardputer LoRa unavailable"

// Heard-node tracking
#define HEARD_NODES_SIZE     16

// Layout tuning per screen size
#if PM_BOARD_LCD_H_RES <= 800
#define MESH_LAYOUT_COMPACT  1
#define MESH_TITLEBAR_H      38
#define MESH_TAB_H           38
#define MESH_INPUT_H         48
#define MESH_RIGHT_W         220
#define MESH_FONT_TAB        (&lv_font_montserrat_12)
#define MESH_FONT_MSG        (&lv_font_montserrat_14)
#define MESH_FONT_SENDER     (&lv_font_montserrat_12)
#define MESH_FONT_HEARD      (&lv_font_montserrat_12)
#define MESH_FONT_LABEL      (&lv_font_montserrat_10)
#define MESH_FONT_INPUT      (&lv_font_montserrat_16)
#else
#define MESH_LAYOUT_COMPACT  0
#define MESH_TITLEBAR_H      44
#define MESH_TAB_H           44
#define MESH_INPUT_H         60
#define MESH_RIGHT_W         300
#define MESH_FONT_TAB        (&lv_font_montserrat_14)
#define MESH_FONT_MSG        (&lv_font_montserrat_16)
#define MESH_FONT_SENDER     (&lv_font_montserrat_14)
#define MESH_FONT_HEARD      (&lv_font_montserrat_14)
#define MESH_FONT_LABEL      (&lv_font_montserrat_12)
#define MESH_FONT_INPUT      (&lv_font_montserrat_18)
#endif

static const char* CHANNEL_NAMES[NUM_CHANNELS] = {
    "#LongFast",
    "#local",
    "#emergency",
    "#pisces",
    "#chan4",
    "#chan5",
    "#chan6",
    "#chan7",
};

typedef struct __attribute__((packed)) {
    uint32_t dest;
    uint32_t from;
    uint32_t id;
    uint32_t flags;
} mesh_header_t;

typedef struct {
    char     sender[SENDER_LEN];
    char     text[TEXT_LEN];
    uint32_t from_node;
    uint32_t when_ms;
    bool     mine;
    bool     system;
} msg_t;

typedef struct {
    msg_t msgs[MAX_MSGS_PER_CHANNEL];
    int   count;
    int   write;       // ring write index
} channel_t;

typedef struct {
    uint32_t node_id;
    int      rssi;
    float    snr;
    uint32_t last_seen_ms;
    bool     valid;
} heard_node_t;

static channel_t*    s_channels = NULL;     // PSRAM
static int           s_active_channel = 0;
static uint32_t      s_my_node_id = 0;
static char          s_input[TEXT_LEN] = "";
static int           s_input_len = 0;
static uint32_t      s_seen_ids[SEEN_IDS_SIZE];
static int           s_seen_idx = 0;
static bool          s_radio_ready = false;
static bool          s_using_cardputer_lora = false;
static bool          s_holds_local_lora = false;
static bool          s_holds_lora_peer = false;
static pm_peer_t*    s_lora_peer = NULL;
static heard_node_t  s_heard[HEARD_NODES_SIZE];
static int           s_heard_replace = 0;
static int           s_input_token = -1;
static bool          s_ui_dirty = true;

// ─────────────────────────────────────────────
//  Helpers — protocol
// ─────────────────────────────────────────────
static uint32_t _make_flags(int hop_limit, bool want_ack, int hop_start) {
    return ((uint32_t)(hop_limit & 0x07)) |
           ((uint32_t)(want_ack ? 1 : 0) << 3) |
           ((uint32_t)(hop_start & 0x07) << 4);
}

static bool _already_seen(uint32_t id) {
    for (int i = 0; i < SEEN_IDS_SIZE; i++)
        if (s_seen_ids[i] == id) return true;
    return false;
}

static void _mark_seen(uint32_t id) {
    s_seen_ids[s_seen_idx] = id;
    s_seen_idx = (s_seen_idx + 1) % SEEN_IDS_SIZE;
}

static float _channel_freq_mhz(int ch) {
    return LORA_FREQ_BASE_MHZ + (ch * 3.125f);
}

// Encode TEXT_MESSAGE_APP protobuf — minimal.
//   0x08 0x01                portnum=1 (varint)
//   0x12 <len-varint> bytes  payload
static int _encode_text_payload(const char* text, uint8_t* out, size_t cap) {
    int len = (int)strlen(text);
    if (len > 230) len = 230;
    if (cap < (size_t)(len + 4)) return 0;
    int n = 0;
    out[n++] = 0x08; out[n++] = 0x01;
    out[n++] = 0x12;
    if (len < 128) {
        out[n++] = (uint8_t)len;
    } else {
        out[n++] = (uint8_t)((len & 0x7F) | 0x80);
        out[n++] = (uint8_t)(len >> 7);
    }
    memcpy(out + n, text, len);
    n += len;
    return n;
}

// Decode text payload — sets text_out. Returns true on success.
static bool _decode_text_payload(const uint8_t* data, int len,
                                  char* text_out, int text_out_len,
                                  uint32_t* portnum_out) {
    text_out[0] = 0;
    *portnum_out = 0;
    int i = 0;
    while (i < len) {
        uint8_t tag       = data[i++];
        uint8_t field_num = tag >> 3;
        uint8_t wire_type = tag & 0x07;
        if (wire_type == 0) {
            uint32_t val = 0; int shift = 0;
            while (i < len) {
                uint8_t b = data[i++];
                val |= ((uint32_t)(b & 0x7F)) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
            }
            if (field_num == 1) *portnum_out = val;
        } else if (wire_type == 2) {
            uint32_t blen = 0; int shift = 0;
            while (i < len) {
                uint8_t b = data[i++];
                blen |= ((uint32_t)(b & 0x7F)) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
            }
            if (field_num == 2 && blen > 0) {
                int copy = (int)blen;
                if (copy > text_out_len - 1) copy = text_out_len - 1;
                memcpy(text_out, data + i, copy);
                text_out[copy] = 0;
            }
            i += blen;
        } else {
            break;
        }
    }
    return text_out[0] != 0;
}

// ─────────────────────────────────────────────
//  Scrollback
// ─────────────────────────────────────────────
static void _add_msg_ex(int ch, const char* sender, const char* text,
                         uint32_t from_node, bool mine, bool system) {
    if (ch < 0 || ch >= NUM_CHANNELS) return;
    if (!s_channels) return;
    channel_t* c = &s_channels[ch];
    msg_t* m = &c->msgs[c->write];
    strncpy(m->sender, sender, SENDER_LEN - 1); m->sender[SENDER_LEN - 1] = 0;
    strncpy(m->text,   text,   TEXT_LEN  - 1);  m->text[TEXT_LEN - 1] = 0;
    m->from_node = from_node;
    m->when_ms   = pm_millis();
    m->mine      = mine;
    m->system    = system;
    c->write = (c->write + 1) % MAX_MSGS_PER_CHANNEL;
    if (c->count < MAX_MSGS_PER_CHANNEL) c->count++;
    if (ch == s_active_channel) s_ui_dirty = true;
}

static void _add_msg(int ch, const char* sender, const char* text,
                      uint32_t from_node, bool mine) {
    _add_msg_ex(ch, sender, text, from_node, mine, false);
}

static void _add_sys(int ch, const char* text) {
    _add_msg_ex(ch, "~sys", text, 0, false, true);
}

// ─────────────────────────────────────────────
//  Heard nodes
// ─────────────────────────────────────────────
static void _note_heard(uint32_t node_id, int rssi, float snr) {
    if (node_id == 0 || node_id == s_my_node_id) return;
    uint32_t now = pm_millis();
    int idx = -1;
    for (int i = 0; i < HEARD_NODES_SIZE; i++) {
        if (s_heard[i].valid && s_heard[i].node_id == node_id) { idx = i; break; }
    }
    if (idx < 0) {
        idx = s_heard_replace;
        s_heard_replace = (s_heard_replace + 1) % HEARD_NODES_SIZE;
        s_heard[idx].valid = true;
        s_heard[idx].node_id = node_id;
    }
    s_heard[idx].rssi = rssi;
    s_heard[idx].snr  = snr;
    s_heard[idx].last_seen_ms = now;
    s_ui_dirty = true;
}

// ─────────────────────────────────────────────
//  LVGL handles
// ─────────────────────────────────────────────
static lv_obj_t* s_scr            = NULL;
static lv_obj_t* s_chip_radio     = NULL;
static lv_obj_t* s_chip_node      = NULL;
static lv_obj_t* s_chip_freq      = NULL;
static lv_obj_t* s_tab_buttons[NUM_CHANNELS];
static lv_obj_t* s_tab_labels[NUM_CHANNELS];
static lv_obj_t* s_msg_log        = NULL;   // scrollable container
static lv_obj_t* s_heard_panel    = NULL;
static lv_obj_t* s_heard_list     = NULL;
static lv_obj_t* s_input_field    = NULL;
static lv_obj_t* s_send_btn       = NULL;
static lv_obj_t* s_input_lbl      = NULL;   // text label inside the field

// Forward declarations
static void _render_messages(void);
static void _render_heard(void);
static void _render_tabs(void);
static void _render_input(void);
static void _render_chips(void);
static bool _radio_init(void);
static void _radio_shutdown(void);
static void _on_lora_rx(const uint8_t* buf, size_t len, int rssi, float snr,
                          void* user);
static void _poll_rx(void);

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────
void pm_app_mesh_messenger_set_channel(int ch) {
    if (ch < 0 || ch >= NUM_CHANNELS) return;
    if (s_using_cardputer_lora && ch != 0) {
        pm_log_w(TAG, "Cardputer LoRa bridge is fixed to LongFast channel 0 for now");
        ch = 0;
    }
    if (ch == s_active_channel) return;
    s_active_channel = ch;
    pm_log_i(TAG, "channel = %d  (%.3f MHz)", ch, _channel_freq_mhz(ch));
    if (!s_using_cardputer_lora && s_holds_local_lora) {
        pm_lora_set_freq_mhz(_channel_freq_mhz(ch));
    }
    s_ui_dirty = true;
}

void pm_app_mesh_messenger_send(const char* text) {
    if (!text || !text[0]) return;
    if (!s_radio_ready) {
        pm_log_w(TAG, "radio not ready");
        _add_sys(s_active_channel, "Radio not ready - cannot send");
        return;
    }

    uint8_t payload[256];
    int payload_len = _encode_text_payload(text, payload, sizeof(payload));
    if (payload_len <= 0) return;

    uint8_t pkt[256 + sizeof(mesh_header_t)];
    mesh_header_t* hdr = (mesh_header_t*)pkt;
    hdr->dest  = MESH_BROADCAST;
    hdr->from  = s_my_node_id;
    hdr->id    = pm_random_u32();
    hdr->flags = _make_flags(MESH_HOP_LIMIT, false, MESH_HOP_LIMIT);
    memcpy(pkt + sizeof(*hdr), payload, payload_len);
    int total = (int)sizeof(*hdr) + payload_len;

    if (s_using_cardputer_lora) {
        if (!s_lora_peer || !s_holds_lora_peer) {
            pm_log_w(TAG, "Cardputer LoRa send without exclusive peer hold");
            _add_sys(s_active_channel, "Cardputer radio not held");
            return;
        }
        esp_err_t err = pm_cardputer_i2c_lora_tx(pkt, (uint8_t)total);
        if (err != ESP_OK) {
            pm_log_w(TAG, "Cardputer LoRa tx failed: %s", esp_err_to_name(err));
            _add_sys(s_active_channel, "TX failed (Cardputer)");
            return;
        }
        pm_log_i(TAG, "Cardputer LoRa TX ch=%d id=%08x len=%d",
                 s_active_channel, (unsigned)hdr->id, total);
    } else {
        pm_lora_set_freq_mhz(_channel_freq_mhz(s_active_channel));
        pm_lora_status_t txr = pm_lora_tx(pkt, total, 2000);
        if (txr != PM_LORA_OK) {
            pm_log_w(TAG, "tx failed: %d", (int)txr);
            _add_sys(s_active_channel, "TX failed");
            return;
        }
        pm_log_d(TAG, "TX ch=%d id=%08x len=%d ok",
                 s_active_channel, (unsigned)hdr->id, total);
    }

    _mark_seen(hdr->id);
    _add_msg(s_active_channel, "~me", text, s_my_node_id, true);
}

// ─────────────────────────────────────────────
//  RX callback
// ─────────────────────────────────────────────
static void _on_lora_rx(const uint8_t* buf, size_t len, int rssi, float snr,
                          void* user) {
    (void)user;
    if (len < sizeof(mesh_header_t) + 2) return;
    const mesh_header_t* hdr = (const mesh_header_t*)buf;
    if (hdr->dest != MESH_BROADCAST && hdr->dest != s_my_node_id) return;
    if (_already_seen(hdr->id)) return;
    _mark_seen(hdr->id);

    _note_heard(hdr->from, rssi, snr);

    char text[TEXT_LEN] = "";
    uint32_t portnum = 0;
    if (!_decode_text_payload(buf + sizeof(mesh_header_t),
                                (int)len - (int)sizeof(mesh_header_t),
                                text, sizeof(text), &portnum)) return;

    char who[16];
    snprintf(who, sizeof(who), "!%08x", (unsigned)hdr->from);
    _add_msg(s_active_channel, who, text, hdr->from, false);
    pm_log_i(TAG, "RX from %08x rssi=%d: %s", (unsigned)hdr->from, rssi, text);
}

static void _poll_rx(void) {
    if (!s_using_cardputer_lora) return;
    for (int i = 0; i < 3; i++) {
        pm_cardputer_i2c_lora_rx_t rx = {0};
        esp_err_t err = pm_cardputer_i2c_lora_rx_pop(&rx);
        if (err != ESP_OK || !rx.available || rx.len == 0) break;
        _on_lora_rx(rx.data, rx.len, rx.rssi, (float)rx.snr_x4 / 4.0f, NULL);
    }
}

// ─────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────
void pm_app_mesh_messenger_input_char(char c) {
    if (c == '\n' || c == '\r') {
        if (s_input_len > 0) {
            s_input[s_input_len] = 0;
            pm_app_mesh_messenger_send(s_input);
        }
        s_input_len = 0;
        s_input[0]  = 0;
    } else if (c == 8 || c == 127) {
        if (s_input_len > 0) s_input_len--;
    } else if (c == '\t') {
        int next = (s_active_channel + 1) % NUM_CHANNELS;
        pm_app_mesh_messenger_set_channel(next);
    } else if (s_input_len < TEXT_LEN - 1) {
        s_input[s_input_len++] = c;
    }
    s_input[s_input_len] = 0;
    s_ui_dirty = true;
}

static void _input_event_handler(const pm_input_event_t* e, void* user) {
    (void)user;
    if (!e) return;
    if (e->kind != PM_INPUT_KEY) return;
    if (!e->down) return;
    // Only handle when mesh is the current app
    const pm_app_t* cur = pm_app_current();
    if (!cur || strcmp(cur->id, "mesh_messenger") != 0) return;
    char c = 0;
    if (e->code == PM_KEY_ENTER)          c = '\n';
    else if (e->code == PM_KEY_BACKSPACE) c = 8;
    else if (e->code == PM_KEY_TAB)       c = '\t';
    else if (e->code >= 0x20 && e->code <= 0x7E) c = (char)e->code;
    if (c) pm_app_mesh_messenger_input_char(c);
}

// ─────────────────────────────────────────────
//  LVGL callbacks
// ─────────────────────────────────────────────
static void _back_cb(lv_event_t* e) {
    (void)e;
    pm_launcher_back_from_app();
}

static void _tab_clicked(lv_event_t* e) {
    int ch = (int)(intptr_t)lv_event_get_user_data(e);
    pm_app_mesh_messenger_set_channel(ch);
}

static void _send_clicked(lv_event_t* e) {
    (void)e;
    if (s_input_len <= 0) return;
    s_input[s_input_len] = 0;
    pm_app_mesh_messenger_send(s_input);
    s_input_len = 0;
    s_input[0] = 0;
    s_ui_dirty = true;
}

// ─────────────────────────────────────────────
//  UI build
// ─────────────────────────────────────────────
static lv_obj_t* _make_tab(lv_obj_t* parent, int idx, const char* name) {
    lv_obj_t* tab = lv_button_create(parent);
    lv_obj_remove_style_all(tab);
    lv_obj_set_flex_grow(tab, 1);
    lv_obj_set_height(tab, MESH_TAB_H - 8);
    lv_obj_set_style_bg_color(tab, PM_C_BG_2, 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tab, 6, 0);
    lv_obj_set_style_border_width(tab, 1, 0);
    lv_obj_set_style_border_color(tab, PM_C_BORDER, 0);
    lv_obj_set_style_pad_all(tab, 4, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tab, _tab_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t* lbl = lv_label_create(tab);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, MESH_FONT_TAB, 0);
    lv_obj_set_style_text_color(lbl, PM_C_FG_DIM, 0);
    lv_obj_center(lbl);
    s_tab_labels[idx] = lbl;
    return tab;
}

static void _build_screen(void) {
    if (s_scr) return;
    s_scr = pm_ui_screen();
    lv_obj_set_layout(s_scr, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_pad_gap(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Titlebar ───────────────────────────────────────────
    lv_obj_t* tbar = pm_ui_titlebar(s_scr, "MESH MESSENGER", _back_cb, NULL);
    lv_obj_set_height(tbar, MESH_TITLEBAR_H);

    // Add status chips on the right of the titlebar
    s_chip_radio = pm_ui_chip(tbar, "RADIO: --", PM_C_WARN);
    lv_obj_align(s_chip_radio, LV_ALIGN_RIGHT_MID, -180, 0);

    s_chip_node = pm_ui_chip(tbar, "!00000000", PM_C_ACCENT_2);
    lv_obj_align(s_chip_node, LV_ALIGN_RIGHT_MID, -12, 0);

    // ── Channel tab row ─────────────────────────────────────
    lv_obj_t* tab_row = lv_obj_create(s_scr);
    lv_obj_remove_style_all(tab_row);
    lv_obj_set_size(tab_row, LV_PCT(100), MESH_TAB_H);
    lv_obj_set_style_bg_color(tab_row, PM_C_BG, 0);
    lv_obj_set_style_bg_opa(tab_row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(tab_row, 8, 0);
    lv_obj_set_style_pad_ver(tab_row, 4, 0);
    lv_obj_set_style_pad_gap(tab_row, 4, 0);
    lv_obj_set_layout(tab_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(tab_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < NUM_CHANNELS; i++) {
        s_tab_buttons[i] = _make_tab(tab_row, i, CHANNEL_NAMES[i]);
    }

    // ── Main content row (msg log left, heard panel right) ──
    lv_obj_t* main_row = lv_obj_create(s_scr);
    lv_obj_remove_style_all(main_row);
    lv_obj_set_width(main_row, LV_PCT(100));
    lv_obj_set_flex_grow(main_row, 1);
    lv_obj_set_style_bg_color(main_row, PM_C_BG, 0);
    lv_obj_set_style_bg_opa(main_row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(main_row, 8, 0);
    lv_obj_set_style_pad_gap(main_row, 8, 0);
    lv_obj_set_layout(main_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(main_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(main_row, LV_OBJ_FLAG_SCROLLABLE);

    // Message log (left, expands)
    s_msg_log = lv_obj_create(main_row);
    lv_obj_remove_style_all(s_msg_log);
    lv_obj_set_flex_grow(s_msg_log, 1);
    lv_obj_set_height(s_msg_log, LV_PCT(100));
    lv_obj_set_style_bg_color(s_msg_log, PM_C_BG_2, 0);
    lv_obj_set_style_bg_opa(s_msg_log, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_msg_log, 8, 0);
    lv_obj_set_style_border_width(s_msg_log, 1, 0);
    lv_obj_set_style_border_color(s_msg_log, PM_C_BORDER, 0);
    lv_obj_set_style_pad_all(s_msg_log, 8, 0);
    lv_obj_set_style_pad_gap(s_msg_log, 4, 0);
    lv_obj_set_layout(s_msg_log, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_msg_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_msg_log, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_msg_log, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_msg_log, LV_SCROLLBAR_MODE_AUTO);

    // Heard nodes (right)
    s_heard_panel = lv_obj_create(main_row);
    lv_obj_remove_style_all(s_heard_panel);
    lv_obj_set_size(s_heard_panel, MESH_RIGHT_W, LV_PCT(100));
    lv_obj_set_style_bg_color(s_heard_panel, PM_C_BG_2, 0);
    lv_obj_set_style_bg_opa(s_heard_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_heard_panel, 8, 0);
    lv_obj_set_style_border_width(s_heard_panel, 1, 0);
    lv_obj_set_style_border_color(s_heard_panel, PM_C_BORDER, 0);
    lv_obj_set_style_pad_all(s_heard_panel, 8, 0);
    lv_obj_set_style_pad_gap(s_heard_panel, 4, 0);
    lv_obj_set_layout(s_heard_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_heard_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_heard_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* heard_hdr = lv_label_create(s_heard_panel);
    lv_label_set_text(heard_hdr, "HEARD NODES");
    lv_obj_set_style_text_font(heard_hdr, MESH_FONT_LABEL, 0);
    lv_obj_set_style_text_color(heard_hdr, PM_C_ACCENT, 0);

    s_heard_list = lv_obj_create(s_heard_panel);
    lv_obj_remove_style_all(s_heard_list);
    lv_obj_set_width(s_heard_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_heard_list, 1);
    lv_obj_set_style_bg_opa(s_heard_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_heard_list, 0, 0);
    lv_obj_set_style_pad_gap(s_heard_list, 3, 0);
    lv_obj_set_layout(s_heard_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_heard_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_heard_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_heard_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_heard_list, LV_SCROLLBAR_MODE_AUTO);

    // ── Input row ───────────────────────────────────────────
    lv_obj_t* input_row = lv_obj_create(s_scr);
    lv_obj_remove_style_all(input_row);
    lv_obj_set_size(input_row, LV_PCT(100), MESH_INPUT_H);
    lv_obj_set_style_bg_color(input_row, PM_C_BG_3, 0);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(input_row, 8, 0);
    lv_obj_set_style_pad_gap(input_row, 8, 0);
    lv_obj_set_layout(input_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    s_chip_freq = pm_ui_chip(input_row, "906.875 MHz", PM_C_ACCENT);

    // Text "field" — actually a styled container with a label inside.
    // Real text edit (via Cardputer keys) updates s_input and we re-render.
    s_input_field = lv_obj_create(input_row);
    lv_obj_remove_style_all(s_input_field);
    lv_obj_set_flex_grow(s_input_field, 1);
    lv_obj_set_height(s_input_field, MESH_INPUT_H - 16);
    lv_obj_set_style_bg_color(s_input_field, PM_C_BG, 0);
    lv_obj_set_style_bg_opa(s_input_field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_input_field, 1, 0);
    lv_obj_set_style_border_color(s_input_field, PM_C_ACCENT, 0);
    lv_obj_set_style_radius(s_input_field, 6, 0);
    lv_obj_set_style_pad_hor(s_input_field, 10, 0);
    lv_obj_clear_flag(s_input_field, LV_OBJ_FLAG_SCROLLABLE);

    s_input_lbl = lv_label_create(s_input_field);
    lv_label_set_text(s_input_lbl, "Type to message...");
    lv_label_set_long_mode(s_input_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_input_lbl, LV_PCT(100));
    lv_obj_set_style_text_font(s_input_lbl, MESH_FONT_INPUT, 0);
    lv_obj_set_style_text_color(s_input_lbl, PM_C_FG_DIM, 0);
    lv_obj_align(s_input_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_send_btn = pm_ui_button(input_row, "SEND", _send_clicked, NULL);
    lv_obj_set_size(s_send_btn, 96, MESH_INPUT_H - 16);
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render_chips(void) {
    if (s_chip_radio) {
        const char* radio_text;
        lv_color_t color;
        if (s_using_cardputer_lora) {
            radio_text = "RADIO: Cardputer";
            color = PM_C_OK;
        } else if (s_holds_local_lora) {
            radio_text = "RADIO: SX1262";
            color = PM_C_OK;
        } else {
            radio_text = "RADIO: NONE";
            color = PM_C_ERR;
        }
        lv_obj_t* lbl = lv_obj_get_child(s_chip_radio, 0);
        if (lbl) {
            lv_label_set_text(lbl, radio_text);
            lv_obj_set_style_text_color(lbl, color, 0);
        }
        lv_obj_set_style_border_color(s_chip_radio, color, 0);
    }
    if (s_chip_node) {
        char buf[16];
        snprintf(buf, sizeof(buf), "!%08x", (unsigned)s_my_node_id);
        lv_obj_t* lbl = lv_obj_get_child(s_chip_node, 0);
        if (lbl) lv_label_set_text(lbl, buf);
    }
    if (s_chip_freq) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f MHz", _channel_freq_mhz(s_active_channel));
        lv_obj_t* lbl = lv_obj_get_child(s_chip_freq, 0);
        if (lbl) lv_label_set_text(lbl, buf);
    }
}

static void _render_tabs(void) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!s_tab_buttons[i]) continue;
        bool active = (i == s_active_channel);
        lv_color_t bg = active ? PM_C_ACCENT : PM_C_BG_2;
        lv_color_t border = active ? PM_C_ACCENT : PM_C_BORDER;
        lv_color_t text_color = active ? PM_C_BG : PM_C_FG_DIM;
        lv_obj_set_style_bg_color(s_tab_buttons[i], bg, 0);
        lv_obj_set_style_border_color(s_tab_buttons[i], border, 0);
        if (s_tab_labels[i]) {
            lv_obj_set_style_text_color(s_tab_labels[i], text_color, 0);
        }
    }
}

static void _render_messages(void) {
    if (!s_msg_log || !s_channels) return;
    lv_obj_clean(s_msg_log);

    channel_t* c = &s_channels[s_active_channel];
    int n = c->count;
    if (n <= 0) {
        lv_obj_t* empty = lv_label_create(s_msg_log);
        lv_label_set_text(empty, "No messages on this channel yet.");
        lv_obj_set_style_text_font(empty, MESH_FONT_MSG, 0);
        lv_obj_set_style_text_color(empty, PM_C_FG_DIM, 0);
        return;
    }

    // Walk ring buffer oldest -> newest
    int start = (c->count < MAX_MSGS_PER_CHANNEL) ? 0 : c->write;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % MAX_MSGS_PER_CHANNEL;
        msg_t* m = &c->msgs[idx];

        lv_obj_t* row = lv_obj_create(s_msg_log);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(row, 2, 0);
        lv_obj_set_style_pad_gap(row, 6, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* who = lv_label_create(row);
        lv_label_set_text(who, m->sender);
        lv_obj_set_style_text_font(who, MESH_FONT_SENDER, 0);
        lv_color_t sender_color =
            m->system ? lv_color_hex(0xFF9933) :
            m->mine   ? PM_C_ACCENT :
                        PM_C_ACCENT_2;
        lv_obj_set_style_text_color(who, sender_color, 0);

        lv_obj_t* body = lv_label_create(row);
        lv_label_set_text(body, m->text);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_style_text_font(body, MESH_FONT_MSG, 0);
        lv_color_t body_color =
            m->system ? PM_C_FG_DIM :
            m->mine   ? PM_C_FG :
                        PM_C_FG;
        lv_obj_set_style_text_color(body, body_color, 0);
    }
    // Auto-scroll to bottom
    lv_obj_scroll_to_y(s_msg_log, LV_COORD_MAX, LV_ANIM_OFF);
}

static void _render_heard(void) {
    if (!s_heard_list) return;
    lv_obj_clean(s_heard_list);

    int shown = 0;
    uint32_t now = pm_millis();
    for (int i = 0; i < HEARD_NODES_SIZE; i++) {
        if (!s_heard[i].valid) continue;
        shown++;

        lv_obj_t* row = lv_obj_create(s_heard_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, PM_C_BG_3, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char line1[24];
        snprintf(line1, sizeof(line1), "!%08x", (unsigned)s_heard[i].node_id);
        lv_obj_t* l1 = lv_label_create(row);
        lv_label_set_text(l1, line1);
        lv_obj_set_style_text_font(l1, MESH_FONT_HEARD, 0);
        lv_obj_set_style_text_color(l1, PM_C_FG, 0);

        char line2[40];
        uint32_t age_s = (now - s_heard[i].last_seen_ms) / 1000;
        snprintf(line2, sizeof(line2), "RSSI %d  SNR %.1f  %us",
                 s_heard[i].rssi, s_heard[i].snr, (unsigned)age_s);
        lv_obj_t* l2 = lv_label_create(row);
        lv_label_set_text(l2, line2);
        lv_obj_set_style_text_font(l2, MESH_FONT_LABEL, 0);
        lv_obj_set_style_text_color(l2, PM_C_FG_DIM, 0);
    }

    if (shown == 0) {
        lv_obj_t* empty = lv_label_create(s_heard_list);
        lv_label_set_text(empty, "(no nodes heard yet)");
        lv_obj_set_style_text_font(empty, MESH_FONT_LABEL, 0);
        lv_obj_set_style_text_color(empty, PM_C_FG_DIM, 0);
    }
}

static void _render_input(void) {
    if (!s_input_lbl) return;
    if (s_input_len > 0) {
        lv_label_set_text(s_input_lbl, s_input);
        lv_obj_set_style_text_color(s_input_lbl, PM_C_FG, 0);
    } else {
        lv_label_set_text(s_input_lbl, "Type to message...");
        lv_obj_set_style_text_color(s_input_lbl, PM_C_FG_DIM, 0);
    }
}

static void _render_all(void) {
    _render_chips();
    _render_tabs();
    _render_messages();
    _render_heard();
    _render_input();
}

// ─────────────────────────────────────────────
//  Radio init / shutdown
// ─────────────────────────────────────────────
static bool _radio_init(void) {
    s_using_cardputer_lora = false;
    s_holds_local_lora = false;
    s_holds_lora_peer = false;
    s_lora_peer = NULL;

    if (pm_radio_kind() == PM_RADIO_SX1262) {
        if (!pm_lora_is_initialized() && pm_lora_init() != PM_LORA_OK) {
            pm_log_w(TAG, "pm_lora_init failed");
            return false;
        }
        if (!pm_lora_take(500, "mesh")) {
            pm_log_w(TAG, "radio busy (held by other app)");
            return false;
        }
        if (pm_lora_set_mode_mesh() != PM_LORA_OK) {
            pm_lora_give();
            return false;
        }
        pm_lora_set_rx_cb(_on_lora_rx, NULL);
        s_holds_local_lora = true;
        pm_log_i(TAG, "mesh mode active on local SX1262, RX armed");
        _add_sys(0, "Local SX1262 ready - LongFast 906.875 MHz");
        return true;
    }

    pm_log_i(TAG, "no local SX1262; trying Cardputer LoRa fallback");
    s_lora_peer = pm_peer_find("lora_mesh", PM_PEER_ROLE_EXCLUSIVE);
    if (!s_lora_peer) {
        pm_log_w(TAG, "no LoRa mesh peer available; local radio is %s",
                 pm_radio_name(pm_radio_kind()));
        _add_sys(0, CARDPUTER_LORA_UNAVAILABLE_TEXT);
        return false;
    }
    s_holds_lora_peer = true;

    if (pm_peer_kind(s_lora_peer) != PM_PEER_KIND_CARDPUTER_I2C) {
        pm_log_w(TAG, "LoRa peer %s has no direct mesh dispatcher yet",
                 pm_peer_name(s_lora_peer));
        pm_peer_release_cap(s_lora_peer, "lora_mesh");
        s_holds_lora_peer = false;
        s_lora_peer = NULL;
        return false;
    }

    if (!pm_cardputer_i2c_link_seen()) {
        pm_log_w(TAG, "Cardputer LoRa peer registered but UART link is not live yet");
        _add_sys(0, "Cardputer UART link not seen");
        pm_peer_release_cap(s_lora_peer, "lora_mesh");
        s_holds_lora_peer = false;
        s_lora_peer = NULL;
        return false;
    }

    if (!(pm_cardputer_i2c_caps() & PM_CARDPUTER_CAP_LORA)) {
        pm_log_w(TAG, "Cardputer bridge is live but does not advertise LoRa caps=0x%08lx",
                 (unsigned long)pm_cardputer_i2c_caps());
        _add_sys(0, "Cardputer has no LoRa capability");
        pm_peer_release_cap(s_lora_peer, "lora_mesh");
        s_holds_lora_peer = false;
        s_lora_peer = NULL;
        return false;
    }

    int rc = pm_peer_call(s_lora_peer, "lora_mesh_start", "\"channel\":0");
    if (rc != 0) {
        pm_log_w(TAG, "Cardputer LoRa mesh start failed rc=%d", rc);
        _add_sys(0, "Cardputer LoRa start failed");
        pm_peer_release_cap(s_lora_peer, "lora_mesh");
        s_holds_lora_peer = false;
        s_lora_peer = NULL;
        return false;
    }

    s_using_cardputer_lora = true;
    s_active_channel = 0;
    pm_log_i(TAG, "mesh mode active on shared %s, LongFast ch0",
             pm_peer_name(s_lora_peer));
    _add_sys(0, "Cardputer LoRa ready - LongFast ch0");
    return true;
}

static void _radio_shutdown(void) {
    if (s_holds_local_lora) {
        pm_lora_set_rx_cb(NULL, NULL);
        pm_lora_give();
        s_holds_local_lora = false;
    }
    if (s_lora_peer && s_using_cardputer_lora) {
        pm_peer_call(s_lora_peer, "lora_stop", NULL);
    }
    if (s_lora_peer && s_holds_lora_peer) {
        pm_peer_release_cap(s_lora_peer, "lora_mesh");
    }
    s_lora_peer = NULL;
    s_holds_lora_peer = false;
    s_using_cardputer_lora = false;
    s_radio_ready = false;
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static void _init(void) {
    if (!s_channels) {
        s_channels = (channel_t*)pm_psram_calloc(NUM_CHANNELS, sizeof(channel_t));
    }
    s_my_node_id = pm_chip_mac_lower32();
    memset(s_heard, 0, sizeof(s_heard));
    s_heard_replace = 0;
    s_input_len = 0;
    s_input[0] = 0;
}

static void _enter(void) {
    if (!s_scr) {
        _build_screen();
    }
    if (s_scr) lv_screen_load(s_scr);
    pm_log_i(TAG, "enter (NodeID=%08x)", (unsigned)s_my_node_id);

    // Welcome / status
    static bool s_welcomed = false;
    if (!s_welcomed) {
        _add_sys(0, "Mesh Messenger ready");
        char idmsg[48];
        snprintf(idmsg, sizeof(idmsg), "Your NodeID: !%08x", (unsigned)s_my_node_id);
        _add_sys(0, idmsg);
        _add_sys(0, "TAB=switch channel  ENTER=send");
        s_welcomed = true;
    }

    s_radio_ready = _radio_init();

    if (s_input_token < 0) {
        s_input_token = pm_input_subscribe(_input_event_handler, NULL);
    }
    s_ui_dirty = true;
    _render_all();
}

static uint32_t s_last_render_ms = 0;
static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    _poll_rx();
    uint32_t now = pm_millis();
    if (s_ui_dirty || now - s_last_render_ms >= 1000) {
        s_last_render_ms = now;
        if (lvgl_port_lock(0)) {
            _render_all();
            lvgl_port_unlock();
        }
        s_ui_dirty = false;
    }
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    if (s_input_token >= 0) {
        pm_input_unsubscribe(s_input_token);
        s_input_token = -1;
    }
    _radio_shutdown();
}

static void _deinit(void) {
    if (s_channels) { pm_psram_free(s_channels); s_channels = NULL; }
    s_scr = NULL;
}

static const pm_app_t _APP = {
    .id           = "mesh_messenger",
    .display_name = "MESH",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_mesh_messenger(void) { return &_APP; }
