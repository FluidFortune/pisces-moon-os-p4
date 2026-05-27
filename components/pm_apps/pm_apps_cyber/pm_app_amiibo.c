// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_amiibo.c — NTAG215 backup / restore
//
//  Amiibos are Nintendo NFC figurines built on NTAG215
//  (540 bytes, 135 pages × 4 bytes). This app backs up your
//  collection to SD and restores them to blank NTAG215 tags.
//
//  Legal note: making personal backups of figurines you own is
//  typical fair-use territory; distributing the dumps is not.
//  The app only writes to SD, never to network.
// ============================================================

#include "pm_app_amiibo.h"
#include "pm_nfc.h"
#include "pm_peer.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t* s_screen;
static lv_obj_t* s_status_lbl;
static lv_obj_t* s_uid_lbl;
static int       s_sub_token = -1;

static void _on_event(const pm_nfc_event_t* e, void* user) {
    (void)user;
    if (!e) return;
    if (e->kind == PM_NFC_EVENT_TAG_SEEN) {
        if (s_uid_lbl) lv_label_set_text(s_uid_lbl, e->uid);
        bool is_ntag = strstr(e->type, "ntag") != NULL;
        if (s_status_lbl) {
            lv_label_set_text(s_status_lbl,
                is_ntag ? "NTAG detected — tap BACKUP" : "Tag is not NTAG21x");
        }
    }
}

static void _backup_cb(lv_event_t* e) {
    (void)e;
    if (!pm_nfc_present()) {
        if (s_status_lbl) lv_label_set_text(s_status_lbl, "PN532 not connected");
        return;
    }
    if (s_status_lbl) lv_label_set_text(s_status_lbl, "Reading NTAG215…");
    // TODO: iterate pages 0..134 via nfc_read_block, accumulate to
    // /sd/amiibo/<uid>.bin
}

static void _restore_cb(lv_event_t* e) {
    (void)e;
    if (s_status_lbl) lv_label_set_text(s_status_lbl,
        "Place blank NTAG215 in field");
    // TODO: file picker for /sd/amiibo/, then page-by-page write
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "AMIIBO", NULL, NULL);
    lv_obj_t* card = pm_ui_card(s_screen);
    s_status_lbl = pm_ui_kv_row(card, "Status", "ready");
    s_uid_lbl    = pm_ui_kv_row(card, "UID",    "—");

    lv_obj_t* row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    pm_ui_button(row, "BACKUP",  _backup_cb,  NULL);
    pm_ui_button(row, "RESTORE", _restore_cb, NULL);

    lv_obj_t* hint = lv_label_create(s_screen);
    lv_label_set_text(hint,
        "Backup amiibos you own to SD card (/sd/amiibo/).\n"
        "Restore to blank NTAG215 tags. Personal use only.");
    lv_obj_set_style_text_color(hint, PM_C_FG_DIM, 0);
}

static void _init (void) { _build_screen(); }
static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    if (s_sub_token < 0) s_sub_token = pm_nfc_subscribe(_on_event, NULL);
}
static void _exit_(void) {
    if (s_sub_token >= 0) { pm_nfc_unsubscribe(s_sub_token); s_sub_token = -1; }
}

static const pm_app_t _APP = {
    .id = "amiibo", .display_name = "AMIIBO",
    .category = PM_CAT_CYBER, .icon_id = 0,
    .init = _init, .enter = _enter, .tick = NULL, .exit = _exit_,
};
const pm_app_t* pm_app_amiibo(void) { return &_APP; }
