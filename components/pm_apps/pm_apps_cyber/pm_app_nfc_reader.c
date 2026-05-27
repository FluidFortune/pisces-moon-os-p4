// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_nfc_reader.c — Read NFC tags
//
//  Subscribes to pm_nfc events. Shows live UID, tag type, SAK.
//  If PN532 is not detected, shows "PN532 not present (modular —
//  plug into C6 UART1 connector to enable)".
// ============================================================

#include "pm_app_nfc_reader.h"
#include "pm_nfc.h"
#include "pm_peer.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_NFC_READER";

static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_uid_lbl;
static lv_obj_t* s_type_lbl;
static lv_obj_t* s_sak_lbl;
static int       s_sub_token = -1;

static void _on_event(const pm_nfc_event_t* e, void* user) {
    (void)user;
    if (!e) return;
    char buf[64];
    switch (e->kind) {
        case PM_NFC_EVENT_PRESENT:
            if (s_status_lbl) lv_label_set_text(s_status_lbl, "PN532 ready");
            break;
        case PM_NFC_EVENT_ABSENT:
            if (s_status_lbl) lv_label_set_text(s_status_lbl, "PN532 unplugged");
            if (s_uid_lbl)    lv_label_set_text(s_uid_lbl, "—");
            if (s_type_lbl)   lv_label_set_text(s_type_lbl, "—");
            if (s_sak_lbl)    lv_label_set_text(s_sak_lbl, "—");
            break;
        case PM_NFC_EVENT_TAG_SEEN:
            if (s_uid_lbl)  lv_label_set_text(s_uid_lbl, e->uid[0] ? e->uid : "(empty)");
            if (s_type_lbl) lv_label_set_text(s_type_lbl, e->type[0] ? e->type : "(unknown)");
            snprintf(buf, sizeof(buf), "0x%02X", e->sak);
            if (s_sak_lbl)  lv_label_set_text(s_sak_lbl, buf);
            pm_log_i(TAG, "Tag seen: %s (%s, SAK 0x%02X)", e->uid, e->type, e->sak);
            break;
        default: break;
    }
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "NFC READER", NULL, NULL);
    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl = pm_ui_kv_row(card, "Status", "checking…");
    s_uid_lbl    = pm_ui_kv_row(card, "UID",    "—");
    s_type_lbl   = pm_ui_kv_row(card, "Type",   "—");
    s_sak_lbl    = pm_ui_kv_row(card, "SAK",    "—");

    lv_obj_t* hint = lv_label_create(s_screen);
    lv_label_set_text(hint,
        "Tap a tag near the PN532 antenna.\n"
        "If you don't see a status, plug the PN532 into the\n"
        "C6 UART1 connector (4-pin, top-right of board).");
    lv_obj_set_style_text_color(hint, PM_C_FG_DIM, 0);
}

static void _init (void) { _build_screen(); }
static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    if (s_sub_token < 0) s_sub_token = pm_nfc_subscribe(_on_event, NULL);
    // Reflect current state on entry
    if (s_status_lbl) lv_label_set_text(s_status_lbl,
        pm_nfc_present() ? "PN532 ready" : "PN532 not present");
}
static void _exit_(void) {
    if (s_sub_token >= 0) { pm_nfc_unsubscribe(s_sub_token); s_sub_token = -1; }
}

static const pm_app_t _APP = {
    .id = "nfc_reader", .display_name = "NFC READER",
    .category = PM_CAT_CYBER, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_nfc_reader(void) { return &_APP; }
