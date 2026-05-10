// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_mesh_messenger.c — Meshtastic-compatible messenger
//
//  Logic from S3 mesh_messenger.cpp ported. The SX1262 driver
//  layer is TODO (same dep as lora_voice). Packet format,
//  protobuf encode/decode, and channel scheduling are real.
//
//  Per-channel scrollback held in PSRAM. Sees-IDs ring buffer
//  prevents echoing our own messages.
// ============================================================

#include "pm_app_mesh_messenger.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_gps_state.h"
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
} msg_t;

typedef struct {
    msg_t msgs[MAX_MSGS_PER_CHANNEL];
    int   count;
    int   write;       // ring write index
} channel_t;

static channel_t* s_channels = NULL;     // PSRAM
static int        s_active_channel = 0;
static uint32_t   s_my_node_id = 0;
static char       s_input[TEXT_LEN] = "";
static int        s_input_len = 0;
static uint32_t   s_seen_ids[SEEN_IDS_SIZE];
static int        s_seen_idx = 0;
static bool       s_radio_ready = false;

// ─────────────────────────────────────────────
//  Helpers
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
//  Channel scrollback
// ─────────────────────────────────────────────
static void _add_msg(int ch, const char* sender, const char* text,
                      uint32_t from_node, bool mine) {
    if (ch < 0 || ch >= NUM_CHANNELS) return;
    if (!s_channels) return;
    channel_t* c = &s_channels[ch];
    msg_t* m = &c->msgs[c->write];
    strncpy(m->sender, sender, SENDER_LEN - 1); m->sender[SENDER_LEN - 1] = 0;
    strncpy(m->text,   text,   TEXT_LEN  - 1);  m->text[TEXT_LEN - 1] = 0;
    m->from_node = from_node;
    m->when_ms   = pm_millis();
    m->mine      = mine;
    c->write = (c->write + 1) % MAX_MSGS_PER_CHANNEL;
    if (c->count < MAX_MSGS_PER_CHANNEL) c->count++;
}

void pm_app_mesh_messenger_set_channel(int ch) {
    if (ch < 0 || ch >= NUM_CHANNELS) return;
    s_active_channel = ch;
    pm_log_i(TAG, "channel = %d  (%.3f MHz)", ch, _channel_freq_mhz(ch));
}

// ─────────────────────────────────────────────
//  Send
// ─────────────────────────────────────────────
void pm_app_mesh_messenger_send(const char* text) {
    if (!text || !text[0]) return;
    if (!s_radio_ready) {
        pm_log_w(TAG, "radio not ready");
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

#include "pm_lora.h"

    // Phase 12 — pm_lora wired
    pm_lora_set_freq_mhz(_channel_freq_mhz(s_active_channel));
    pm_lora_status_t txr = pm_lora_tx(pkt, total, 2000);
    if (txr != PM_LORA_OK) {
        pm_log_w(TAG, "tx failed: %d", (int)txr);
    } else {
        pm_log_d(TAG, "TX ch=%d id=%08x len=%d ok",
                 s_active_channel, (unsigned)hdr->id, total);
    }

    _mark_seen(hdr->id);
    _add_msg(s_active_channel, "~me", text, s_my_node_id, true);
}

// ─────────────────────────────────────────────
//  RX callback — wired through pm_lora_set_rx_cb
//
//  Parses header. If dest is broadcast or matches our NodeID
//  and id is not in the seen ring, decode payload and queue
//  the chat message. Frame-relay (forward to other channels)
//  intentionally not implemented yet.
// ─────────────────────────────────────────────
static void _on_lora_rx(const uint8_t* buf, size_t len, int rssi, float snr,
                          void* user) {
    (void)user; (void)snr;
    if (len < sizeof(mesh_header_t) + 2) return;
    const mesh_header_t* hdr = (const mesh_header_t*)buf;
    if (hdr->dest != MESH_BROADCAST && hdr->dest != s_my_node_id) return;
    if (_already_seen(hdr->id)) return;
    _mark_seen(hdr->id);

    char text[TEXT_LEN] = "";
    uint32_t portnum = 0;
    if (!_decode_text_payload(buf + sizeof(mesh_header_t),
                                (int)len - (int)sizeof(mesh_header_t),
                                text, sizeof(text), &portnum)) return;

    char who[16];
    snprintf(who, sizeof(who), "~%08x", (unsigned)hdr->from);
    _add_msg(s_active_channel, who, text, hdr->from, false);
    pm_log_i(TAG, "RX from %08x rssi=%d: %s", (unsigned)hdr->from, rssi, text);
}

static void _poll_rx(void) { /* RX is callback-driven now */ }

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
        pm_app_mesh_messenger_set_channel((s_active_channel + 1) % NUM_CHANNELS);
    } else if (s_input_len < TEXT_LEN - 1) {
        s_input[s_input_len++] = c;
    }
    s_input[s_input_len] = 0;
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: channel tabs row 0..7 (active highlighted),
    //            scrolling message log for s_active_channel,
    //            input line (s_input), [SEND] button,
    //            footer with NodeID + freq.
    pm_log_d(TAG, "ch=%d msgs=%d", s_active_channel,
             s_channels ? s_channels[s_active_channel].count : 0);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
#include "pm_radio.h"

static bool _radio_init(void) {
    // Phase 13: only attempt if pm_radio detected an SX1262.
    // Anything else means we don't have a LoRa-capable module.
    if (pm_radio_kind() != PM_RADIO_SX1262) {
        pm_log_w(TAG, "mesh requires SX1262; current radio: %s",
                 pm_radio_name(pm_radio_kind()));
        return false;
    }
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
    pm_log_i(TAG, "mesh mode active, RX armed");
    return true;
}

static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("MESH",
        "MESH app — UI ready");
}

static void _init(void) {
    if (!s_channels) {
        s_channels = (channel_t*)pm_psram_calloc(NUM_CHANNELS, sizeof(channel_t));
    }
    s_my_node_id = pm_chip_mac_lower32();
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter (NodeID=%08x)", (unsigned)s_my_node_id);
    s_radio_ready = _radio_init();
    _render();
}

static uint32_t s_last_render_ms = 0;
static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    _poll_rx();
    uint32_t now = pm_millis();
    if (now - s_last_render_ms >= 250) {
        s_last_render_ms = now;
        _render();
    }
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static void _deinit(void) {
    if (s_channels) { pm_psram_free(s_channels); s_channels = NULL; }
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
