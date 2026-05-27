// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


#include "pm_nfc.h"
#include "pm_hal.h"
#include "pm_peer.h"
#include <string.h>

static const char* TAG = "PM_NFC";

#define MAX_SUBS 6

typedef struct {
    pm_nfc_handler_t cb;
    void*            user;
    bool             active;
} sub_slot_t;

static sub_slot_t s_subs[MAX_SUBS];
static bool       s_present = false;
static char       s_last_uid[24] = "";
static char       s_last_type[20] = "";

void pm_nfc_init(void) {
    memset(s_subs, 0, sizeof(s_subs));
    s_present = false;
    s_last_uid[0] = 0;
    s_last_type[0] = 0;
    pm_log_i(TAG, "P4-side NFC consumer ready (awaiting C6 events)");
}

bool        pm_nfc_present (void) { return s_present; }
const char* pm_nfc_last_uid (void) { return s_last_uid; }
const char* pm_nfc_last_type(void) { return s_last_type; }

int pm_nfc_subscribe(pm_nfc_handler_t cb, void* user) {
    if (!cb) return -1;
    for (int i = 0; i < MAX_SUBS; i++) {
        if (!s_subs[i].active) {
            s_subs[i].cb = cb; s_subs[i].user = user; s_subs[i].active = true;
            return i;
        }
    }
    return -1;
}

void pm_nfc_unsubscribe(int token) {
    if (token < 0 || token >= MAX_SUBS) return;
    s_subs[token].active = false;
    s_subs[token].cb = NULL;
}

static void _broadcast(const pm_nfc_event_t* e) {
    for (int i = 0; i < MAX_SUBS; i++) {
        if (s_subs[i].active && s_subs[i].cb) {
            s_subs[i].cb(e, s_subs[i].user);
        }
    }
}

// Capabilities advertised when PN532 is present
static const char* NFC_CAPS[] = {
    "nfc_read", "nfc_write", "nfc_emulate", NULL,
};

void pm_nfc_on_present(const char* fw_hex) {
    (void)fw_hex;
    s_present = true;
    pm_peer_announce(PM_PEER_KIND_NFC_PN532, "PN532 NFC",
                      NFC_CAPS);
    pm_log_i(TAG, "PN532 present (registered as peer)");
    pm_nfc_event_t e = { .kind = PM_NFC_EVENT_PRESENT };
    _broadcast(&e);
}

void pm_nfc_on_absent(void) {
    s_present = false;
    s_last_uid[0] = 0;
    pm_peer_withdraw(PM_PEER_KIND_NFC_PN532);
    pm_log_i(TAG, "PN532 absent (peer withdrawn)");
    pm_nfc_event_t e = { .kind = PM_NFC_EVENT_ABSENT };
    _broadcast(&e);
}

void pm_nfc_on_seen(const char* uid_hex, const char* type, int sak) {
    if (uid_hex) {
        strncpy(s_last_uid, uid_hex, sizeof(s_last_uid) - 1);
        s_last_uid[sizeof(s_last_uid) - 1] = 0;
    }
    if (type) {
        strncpy(s_last_type, type, sizeof(s_last_type) - 1);
        s_last_type[sizeof(s_last_type) - 1] = 0;
    }
    pm_nfc_event_t e = { .kind = PM_NFC_EVENT_TAG_SEEN, .sak = (uint8_t)sak };
    if (uid_hex) strncpy(e.uid, uid_hex, sizeof(e.uid) - 1);
    if (type)    strncpy(e.type, type, sizeof(e.type) - 1);
    _broadcast(&e);
}

void pm_nfc_on_data(const char* uid_hex, int block, const char* data_hex) {
    pm_nfc_event_t e = { .kind = PM_NFC_EVENT_DATA, .block = block };
    if (uid_hex) strncpy(e.uid, uid_hex, sizeof(e.uid) - 1);
    if (data_hex) strncpy(e.data_hex, data_hex, sizeof(e.data_hex) - 1);
    _broadcast(&e);
}
