// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_peer.c — Registry implementation
//
//  Small fixed-size table. Peers register themselves at boot
//  (permanent fixtures) or via bridge announce events
//  (hot-plug optional modules). Apps lookup by capability +
//  role; the registry walks the table and returns the best
//  match.
//
//  No allocation after init; all storage is static.
// ============================================================

#include "pm_peer.h"
#include "pm_board.h"
#include "pm_hal.h"
#include "pm_cardputer_i2c.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_PEER";

#define MAX_PEERS 16
#define MAX_CAPS_PER_PEER 12

// ── Peer struct (internal) ──────────────────────────────────
struct pm_peer_s {
    bool             active;
    pm_peer_kind_t   kind;
    char             name[40];
    const char*      caps[MAX_CAPS_PER_PEER + 1]; // NULL-terminated
    int              cap_count;
    bool             is_primary;
    bool             held_exclusive;
};

static struct pm_peer_s s_peers[MAX_PEERS];
static int              s_peer_count = 0;

// ─────────────────────────────────────────────
//  Capability tables — what each peer offers
//
//  These are baked in; the C6 always offers WiFi/BLE/HTTP
//  whether NFC is plugged in or not. NFC capabilities are
//  added/removed as the PN532 comes and goes.
// ─────────────────────────────────────────────
static const char* CAPS_C6_BASE[] = {
    "wifi_scan",
#if !PM_BOARD_CARDPUTER_RADIO_BRIDGE
    "wifi_capture",
#endif
    "wifi_connect",
    "ble_scan", "ble_gatt", "ble_hid_host",
    "http_get", "http_post",
    NULL,
};

static const char* CAPS_C6_WITH_NFC[] = {
    "wifi_scan",
#if !PM_BOARD_CARDPUTER_RADIO_BRIDGE
    "wifi_capture",
#endif
    "wifi_connect",
    "ble_scan", "ble_gatt", "ble_hid_host",
    "http_get", "http_post",
    "nfc_read", "nfc_write", "nfc_emulate",
    NULL,
};

static const char* CAPS_TBEAM[] = {
    "wifi_scan", "wifi_capture", "wifi_connect",
    "ble_scan", "ble_gatt",
    "lora_tx", "lora_rx", "lora_mesh",
    NULL,
};

static const char* CAPS_SLOT_SX1262[] = {
    "lora_tx", "lora_rx", "lora_mesh", "lora_voice", NULL,
};

static const char* CAPS_SLOT_NRF24[] = {
    "nrf24_sniff", "nrf24_tx", NULL,
};

static const char* CAPS_CAMERA[] = {
    "camera_snapshot", "camera_stream", "camera_barcode", NULL,
};

static const char* CAPS_CARDPUTER_I2C[] = {
    "gps_remote",
    "gps_status",
    "lora_tx",
    "lora_rx",
    "lora_mesh",
    "wifi_scan",
    "wifi_capture",
    "wifi_promisc",
    "ble_scan",
    "ble_gatt",
    "keyboard_hid",
    "s3_module",
    NULL,
};

// ─────────────────────────────────────────────
//  Registration helpers
// ─────────────────────────────────────────────
static struct pm_peer_s* _alloc_slot(void) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!s_peers[i].active) return &s_peers[i];
    }
    return NULL;
}

static struct pm_peer_s* _find_by_kind(pm_peer_kind_t k) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (s_peers[i].active && s_peers[i].kind == k) {
            return &s_peers[i];
        }
    }
    return NULL;
}

static void _set_caps(struct pm_peer_s* p, const char* const* caps) {
    int i = 0;
    while (caps[i] && i < MAX_CAPS_PER_PEER) {
        p->caps[i] = caps[i];
        i++;
    }
    p->caps[i] = NULL;
    p->cap_count = i;
}

static const char* _kind_name(pm_peer_kind_t k) {
    switch (k) {
        case PM_PEER_KIND_C6_GHOST:    return "C6 Ghost Engine";
        case PM_PEER_KIND_TBEAM_S3:    return "T-Beam Supreme S3";
        case PM_PEER_KIND_SLOT_SX1262: return "Slot SX1262";
        case PM_PEER_KIND_SLOT_NRF24:  return "Slot nRF24";
        case PM_PEER_KIND_SLOT_H2:     return "Slot ESP32-H2";
        case PM_PEER_KIND_SLOT_C6:     return "Slot ESP32-C6";
        case PM_PEER_KIND_SLOT_HALOW:  return "Slot Wi-Fi HaLow";
        case PM_PEER_KIND_NFC_PN532:   return "PN532 NFC";
        case PM_PEER_KIND_CAMERA_CSI:  return "CSI Camera";
        case PM_PEER_KIND_BT_GAMEPAD:  return "BT Gamepad";
        case PM_PEER_KIND_BT_KEYBOARD: return "BT Keyboard";
        case PM_PEER_KIND_CARDPUTER_I2C: return "Cardputer UART Module";
    }
    return "(unknown)";
}

// ─────────────────────────────────────────────
//  Public API — registration
// ─────────────────────────────────────────────
void pm_peer_announce(pm_peer_kind_t kind, const char* name,
                       const char* const* caps) {
    struct pm_peer_s* p = _find_by_kind(kind);
    if (!p) p = _alloc_slot();
    if (!p) { pm_log_w(TAG, "registry full, can't add %s", _kind_name(kind)); return; }

    p->active = true;
    p->kind   = kind;
    strncpy(p->name, name ? name : _kind_name(kind), sizeof(p->name) - 1);
    p->name[sizeof(p->name) - 1] = 0;
    if (caps) _set_caps(p, caps);

    // Mark C6 + T-Beam as primary candidates for their respective
    // radios. C6 is always primary for WiFi/BLE. Slot LoRa is
    // primary for LoRa when present; otherwise T-Beam's LoRa is.
    p->is_primary = (kind == PM_PEER_KIND_C6_GHOST)
                  || (kind == PM_PEER_KIND_SLOT_SX1262)
                  || (kind == PM_PEER_KIND_SLOT_NRF24)
                  || (kind == PM_PEER_KIND_NFC_PN532)
                  || (kind == PM_PEER_KIND_CAMERA_CSI)
                  || (PM_BOARD_CARDPUTER_RADIO_BRIDGE &&
                      kind == PM_PEER_KIND_CARDPUTER_I2C);

    if (p->active && _find_by_kind(kind) == p) s_peer_count++;

    pm_log_i(TAG, "+ peer registered: %s (%d capabilities)",
             p->name, p->cap_count);
}

void pm_peer_withdraw(pm_peer_kind_t kind) {
    struct pm_peer_s* p = _find_by_kind(kind);
    if (!p) return;
    pm_log_i(TAG, "- peer withdrawn: %s", p->name);
    p->active = false;
    p->cap_count = 0;
    s_peer_count--;
}

// ─────────────────────────────────────────────
//  Public API — auto-detect at boot
// ─────────────────────────────────────────────
int pm_peer_init_auto(void) {
    pm_log_i(TAG, "Probing peers (modular auto-detect)…");
    memset(s_peers, 0, sizeof(s_peers));
    s_peer_count = 0;

    // 1. Permanent fixture: C6 Ghost Engine. Always present.
    //    Starts with WiFi/BLE only; NFC capabilities are added
    //    if/when the PN532 announces itself.
    pm_peer_announce(PM_PEER_KIND_C6_GHOST,
                      "C6 Ghost Engine", CAPS_C6_BASE);

    // 2. Slot module — pm_radio's auto-detect already ran. If it
    //    found something, register it here.
    extern int pm_radio_kind(void);
    int slot_kind = pm_radio_kind();
    if (slot_kind == 1)        // PM_RADIO_SX1262
        pm_peer_announce(PM_PEER_KIND_SLOT_SX1262,
                          "Slot SX1262", CAPS_SLOT_SX1262);
    else if (slot_kind == 2)   // PM_RADIO_NRF24
        pm_peer_announce(PM_PEER_KIND_SLOT_NRF24,
                          "Slot nRF24", CAPS_SLOT_NRF24);

    // 3. T-Beam — probed by sending "ping" over UART2 and
    //    waiting briefly for "pong". If silent, no T-Beam.
    //    (The actual probe is in pm_c6_bridge for now; bridge
    //    receiver registers the peer when the first event
    //    arrives. Apps see "no T-Beam available" until then.)

    // 4. Camera — probed via I2C address scan for SC2336.
    //    pm_camera_probe() is implemented in pm_camera; here
    //    we'd call it. For now we just log the intent.
    extern bool pm_camera_present(void);
    if (pm_camera_present()) {
        pm_peer_announce(PM_PEER_KIND_CAMERA_CSI,
                          "CSI Camera", CAPS_CAMERA);
    }

    // 5. Cardputer ADV companion module over the external I2C bus.
    //    Boot probe only; absence is normal and quiet.
    if (pm_cardputer_i2c_init_auto()) {
        pm_peer_announce(PM_PEER_KIND_CARDPUTER_I2C,
                         "Cardputer ADV I2C Module",
                         CAPS_CARDPUTER_I2C);
    }

    // 6. NFC and BT-HID peers come and go via bridge events;
    //    not probed here.

    pm_log_i(TAG, "%d peers ready", s_peer_count);
    return s_peer_count;
}

// ─────────────────────────────────────────────
//  Public API — lookup
// ─────────────────────────────────────────────
static bool _peer_has_cap(const struct pm_peer_s* p, const char* cap) {
    for (int i = 0; i < p->cap_count; i++) {
        if (strcmp(p->caps[i], cap) == 0) return true;
    }
    return false;
}

static struct pm_peer_s* _find_ble_scan_preferred(void) {
    struct pm_peer_s* cardputer = NULL;
    struct pm_peer_s* tbeam = NULL;
    struct pm_peer_s* c6 = NULL;

    for (int i = 0; i < MAX_PEERS; i++) {
        struct pm_peer_s* p = &s_peers[i];
        if (!p->active || p->held_exclusive) continue;
        if (!_peer_has_cap(p, "ble_scan")) continue;

        if (p->kind == PM_PEER_KIND_CARDPUTER_I2C) {
            if (pm_cardputer_i2c_link_seen()) cardputer = p;
        } else if (p->kind == PM_PEER_KIND_TBEAM_S3) {
            tbeam = p;
        } else if (p->kind == PM_PEER_KIND_C6_GHOST) {
            c6 = p;
        }
    }

    return cardputer ? cardputer : (tbeam ? tbeam : c6);
}

pm_peer_t* pm_peer_find(const char* capability, pm_peer_role_t role) {
    if (!capability) return NULL;

    if (strcmp(capability, "ble_scan") == 0 &&
        role != PM_PEER_ROLE_SECONDARY) {
        struct pm_peer_s* preferred = _find_ble_scan_preferred();
        if (preferred && role == PM_PEER_ROLE_EXCLUSIVE) {
            preferred->held_exclusive = true;
        }
        if (preferred) return preferred;
    }

    struct pm_peer_s* primary = NULL;
    struct pm_peer_s* secondary = NULL;

    for (int i = 0; i < MAX_PEERS; i++) {
        struct pm_peer_s* p = &s_peers[i];
        if (!p->active) continue;
        if (!_peer_has_cap(p, capability)) continue;
        if (p->is_primary) {
            if (!primary) primary = p;
        } else {
            if (!secondary) secondary = p;
        }
    }

    switch (role) {
        case PM_PEER_ROLE_PRIMARY:   return primary;
        case PM_PEER_ROLE_SECONDARY: return secondary;
        case PM_PEER_ROLE_EXCLUSIVE:
            if (primary && !primary->held_exclusive) {
                primary->held_exclusive = true;
                return primary;
            }
            return NULL;
        case PM_PEER_ROLE_ANY:
        default:
            return primary ? primary : secondary;
    }
}

void pm_peer_release(pm_peer_t* p) {
    if (p) p->held_exclusive = false;
}

// ─────────────────────────────────────────────
//  Public API — introspection
// ─────────────────────────────────────────────
pm_peer_kind_t pm_peer_kind_of(const pm_peer_t* p) { return p->kind; }
const char*    pm_peer_name   (const pm_peer_t* p) { return p->name; }
pm_peer_kind_t pm_peer_kind   (const pm_peer_t* p) { return p->kind; }

int pm_peer_count(void) { return s_peer_count; }

const pm_peer_t* pm_peer_at(int idx) {
    int seen = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (s_peers[i].active) {
            if (seen == idx) return &s_peers[i];
            seen++;
        }
    }
    return NULL;
}

const char* const* pm_peer_capabilities(const pm_peer_t* p) {
    return p ? (const char* const*)p->caps : NULL;
}

// ─────────────────────────────────────────────
//  Public API — generic call
//
//  Currently a thin dispatcher: bridge-resident peers forward
//  ops to the bridge as JSON commands; future in-process peers
//  (e.g., camera, slot radios) can be wired here directly.
// ─────────────────────────────────────────────
int pm_peer_call(pm_peer_t* p, const char* op, const char* params) {
    if (!p || !op) return -1;
    extern void pm_c6_cmd_send_raw(const char* json_line);
    char buf[256];

    switch (p->kind) {
        case PM_PEER_KIND_C6_GHOST:
            // Map op to a C6 bridge command
            if (params)
                snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\",%s}\n", op, params);
            else
                snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}\n", op);
            pm_c6_cmd_send_raw(buf);
            return 0;
        case PM_PEER_KIND_TBEAM_S3:
            if (strcmp(op, "ble_scan_start") == 0 || strcmp(op, "ble_start") == 0) {
                extern void pm_tbeam_send_cmd(const char* json_line);
                pm_tbeam_send_cmd("{\"cmd\":\"ble_scan_start\"}");
                return 0;
            }
            if (strcmp(op, "ble_scan_stop") == 0 || strcmp(op, "ble_stop") == 0) {
                extern void pm_tbeam_send_cmd(const char* json_line);
                pm_tbeam_send_cmd("{\"cmd\":\"ble_scan_stop\"}");
                return 0;
            }
            pm_log_d(TAG, "T-Beam dispatch (stub): %s", op);
            return 0;
        case PM_PEER_KIND_CARDPUTER_I2C:
            return pm_cardputer_i2c_call(op, params);
        default:
            pm_log_w(TAG, "no dispatcher for peer kind %d", p->kind);
            return -2;
    }
}
