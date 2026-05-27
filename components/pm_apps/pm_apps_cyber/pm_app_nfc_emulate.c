// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_nfc_emulate.c — PN532 card emulation
//
//  The PN532 can act as an ISO14443-A card. This app loads a
//  previously saved tag dump from /sd/nfc/<uid>.bin and tells
//  the C6 to start emulation.
//
//  In-app disclaimer makes the legal framing explicit. The app
//  refuses to launch unless the user taps "I understand" first.
// ============================================================

#include "pm_app_nfc_emulate.h"
#include "pm_nfc.h"
#include "pm_peer.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include "lvgl.h"

static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static bool      s_emulating = false;
static bool      s_disclaimer_accepted = false;

static void _start_cb(lv_event_t* e) {
    (void)e;
    if (!s_disclaimer_accepted) {
        if (s_status_lbl) lv_label_set_text(s_status_lbl,
            "Acknowledge the disclaimer first");
        return;
    }
    if (!pm_nfc_present()) {
        if (s_status_lbl) lv_label_set_text(s_status_lbl,
            "PN532 not connected");
        return;
    }
    // TODO: file picker for /sd/nfc/; for now, emit a stub
    pm_c6_cmd_send_raw("{\"cmd\":\"nfc_emulate\",\"uid\":\"DEADBEEF\"}\n");
    s_emulating = true;
    if (s_status_lbl) lv_label_set_text(s_status_lbl, "Emulating…");
}

static void _stop_cb(lv_event_t* e) {
    (void)e;
    pm_c6_cmd_send_raw("{\"cmd\":\"nfc_stop\"}\n");
    s_emulating = false;
    if (s_status_lbl) lv_label_set_text(s_status_lbl, "Stopped");
}

static void _accept_cb(lv_event_t* e) {
    (void)e;
    s_disclaimer_accepted = true;
    if (s_status_lbl) lv_label_set_text(s_status_lbl,
        "Disclaimer accepted — ready");
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "NFC EMULATE", NULL, NULL);

    lv_obj_t* warn_card = pm_ui_card(s_screen);
    lv_obj_t* warn = lv_label_create(warn_card);
    lv_label_set_text(warn,
        "LEGAL NOTICE\n\n"
        "Card emulation lets this device pretend to be an NFC tag.\n"
        "Emulating credentials you do not own (hotel keys, work badges,\n"
        "transit cards, etc.) is illegal in most jurisdictions and may\n"
        "constitute fraud or unauthorized access.\n\n"
        "Use only for:\n"
        "  • Replaying your own access tags\n"
        "  • Written authorized security research\n"
        "  • Tags marketed for emulation (test cards)\n\n"
        "By tapping I UNDERSTAND, you accept responsibility for\n"
        "lawful use of this feature.");
    lv_obj_set_style_text_color(warn, PM_C_WARN, 0);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(warn, LV_PCT(100));

    pm_ui_button(s_screen, "I UNDERSTAND", _accept_cb, NULL);

    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl = pm_ui_kv_row(card, "Status", "awaiting disclaimer");

    lv_obj_t* row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    pm_ui_button(row, "START", _start_cb, NULL);
    pm_ui_button(row, "STOP",  _stop_cb,  NULL);
}

static void _init (void) { _build_screen(); }
static void _enter(void) { if (s_screen) lv_screen_load(s_screen); }
static void _exit_(void) {
    if (s_emulating) {
        pm_c6_cmd_send_raw("{\"cmd\":\"nfc_stop\"}\n");
        s_emulating = false;
    }
}

static const pm_app_t _APP = {
    .id = "nfc_emulate", .display_name = "NFC EMULATE",
    .category = PM_CAT_CYBER, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_nfc_emulate(void) { return &_APP; }
