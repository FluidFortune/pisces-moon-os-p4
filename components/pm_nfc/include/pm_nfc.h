// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_nfc.h — P4-side API for NFC events
//
//  The PN532 itself lives on the C6's UART1. The C6's ghost_nfc
//  module owns the driver; events flow over the bridge. This
//  P4-side component is the consumer:
//
//    - Receives nfc_present / nfc_absent / nfc_seen / nfc_data
//      events from pm_c6_bridge
//    - Maintains a small state cache (last UID, type, sak,
//      attached/detached)
//    - Notifies subscribers (apps) when a tag appears, when
//      a block read completes, etc.
//    - Registers / withdraws the PN532 as a pm_peer when the
//      module is hot-plugged
//
//  Apps use pm_peer_find("nfc_read") to check availability,
//  pm_peer_call() to issue commands, and pm_nfc_subscribe() to
//  receive events.
// ============================================================

#ifndef PM_NFC_H
#define PM_NFC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_NFC_EVENT_PRESENT  = 0,
    PM_NFC_EVENT_ABSENT   = 1,
    PM_NFC_EVENT_TAG_SEEN = 2,
    PM_NFC_EVENT_DATA     = 3,
} pm_nfc_event_kind_t;

typedef struct {
    pm_nfc_event_kind_t kind;
    char     uid[24];       // hex, NUL-terminated
    char     type[20];      // "mifare_classic", "ntag", "iso14443a"
    uint8_t  sak;
    int      block;         // for DATA events
    char     data_hex[64];  // for DATA events
} pm_nfc_event_t;

typedef void (*pm_nfc_handler_t)(const pm_nfc_event_t* e, void* user);

// ── Init / lifecycle ────────────────────────────────────────
void pm_nfc_init(void);

// ── State accessors ─────────────────────────────────────────
bool        pm_nfc_present(void);
const char* pm_nfc_last_uid(void);
const char* pm_nfc_last_type(void);

// ── Subscription ────────────────────────────────────────────
int  pm_nfc_subscribe(pm_nfc_handler_t cb, void* user);
void pm_nfc_unsubscribe(int token);

// ── Bridge event handlers (called by pm_c6_bridge) ──────────
void pm_nfc_on_present(const char* fw_hex);
void pm_nfc_on_absent(void);
void pm_nfc_on_seen(const char* uid_hex, const char* type, int sak);
void pm_nfc_on_data(const char* uid_hex, int block, const char* data_hex);

#ifdef __cplusplus
}
#endif

#endif  // PM_NFC_H
