// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


#include "pm_app_nfc_clone.h"
#include "pm_nfc.h"
#include "pm_peer.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "pm_c6_bridge.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_NFC_CLONE";

static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_uid_lbl;
static lv_obj_t* s_progress_lbl;
static int       s_sub_token = -1;
static bool      s_reading = false;
static bool      s_writing = false;
static int       s_current_block = 0;
static char      s_saved_uid[24] = "";

static void _set_status(const char* s) {
    if (s_status_lbl) lv_label_set_text(s_status_lbl, s);
}

static void _on_event(const pm_nfc_event_t* e, void* user) {
    (void)user;
    if (!e) return;
    char buf[80];
    switch (e->kind) {
        case PM_NFC_EVENT_TAG_SEEN:
            if (s_uid_lbl) lv_label_set_text(s_uid_lbl, e->uid);
            if (s_reading) {
                strncpy(s_saved_uid, e->uid, sizeof(s_saved_uid) - 1);
                _set_status("Reading blocks…");
                // Trigger first block read; chain on data events
                s_current_block = 0;
                pm_c6_cmd_send_raw("{\"cmd\":\"nfc_read_block\",\"block\":0,\"keyA\":\"FFFFFFFFFFFF\"}\n");
            } else if (s_writing) {
                _set_status("Blank tag in field, writing…");
                s_current_block = 0;
                // TODO: read saved blocks from /sd/nfc/<uid>.bin and
                // issue nfc_write_block commands
            }
            break;
        case PM_NFC_EVENT_DATA:
            if (s_reading) {
                snprintf(buf, sizeof(buf), "Read block %d", e->block);
                if (s_progress_lbl) lv_label_set_text(s_progress_lbl, buf);
                // TODO: append e->data_hex to /sd/nfc/<uid>.bin
                s_current_block++;
                if (s_current_block >= 64) {  // MIFARE Classic 1K
                    s_reading = false;
                    _set_status("Save complete");
                } else {
                    snprintf(buf, sizeof(buf),
                        "{\"cmd\":\"nfc_read_block\",\"block\":%d,\"keyA\":\"FFFFFFFFFFFF\"}\n",
                        s_current_block);
                    pm_c6_cmd_send_raw(buf);
                }
            }
            break;
        case PM_NFC_EVENT_ABSENT:
            _set_status("PN532 disconnected");
            s_reading = s_writing = false;
            break;
        default: break;
    }
}

static void _read_cb(lv_event_t* e) {
    (void)e;
    if (!pm_nfc_present()) { _set_status("PN532 not connected"); return; }
    s_reading = true; s_writing = false;
    s_current_block = 0;
    _set_status("Place SOURCE tag in field");
    pm_c6_cmd_send_raw("{\"cmd\":\"nfc_read_uid\"}\n");
}

static void _write_cb(lv_event_t* e) {
    (void)e;
    if (!pm_nfc_present()) { _set_status("PN532 not connected"); return; }
    if (!s_saved_uid[0])    { _set_status("Read a source tag first"); return; }
    s_writing = true; s_reading = false;
    _set_status("Place BLANK tag in field");
    pm_c6_cmd_send_raw("{\"cmd\":\"nfc_read_uid\"}\n");
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "NFC CLONE", NULL, NULL);
    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl   = pm_ui_kv_row(card, "Status",   "ready");
    s_uid_lbl      = pm_ui_kv_row(card, "Last UID", "—");
    s_progress_lbl = pm_ui_kv_row(card, "Progress", "—");

    lv_obj_t* row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    pm_ui_button(row, "READ",  _read_cb,  NULL);
    pm_ui_button(row, "WRITE", _write_cb, NULL);

    lv_obj_t* warn = lv_label_create(s_screen);
    lv_label_set_text(warn,
        "USE ON OWNED OR AUTHORIZED TAGS ONLY.\n"
        "Cloning credentials you don't own is illegal in most\n"
        "jurisdictions. This tool is for backup/restore of\n"
        "your own tags and authorized security research.");
    lv_obj_set_style_text_color(warn, PM_C_WARN, 0);
}

static void _init (void) { _build_screen(); }
static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    if (s_sub_token < 0) s_sub_token = pm_nfc_subscribe(_on_event, NULL);
}
static void _exit_(void) {
    if (s_sub_token >= 0) { pm_nfc_unsubscribe(s_sub_token); s_sub_token = -1; }
    s_reading = s_writing = false;
}

static const pm_app_t _APP = {
    .id = "nfc_clone", .display_name = "NFC CLONE",
    .category = PM_CAT_CYBER, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_nfc_clone(void) { return &_APP; }
