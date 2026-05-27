// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_c6_flasher.c — Ghost Engine firmware flasher app
//
//  Surfaces pm_c6_programmer through a touch UI. Lets the
//  user pick a firmware blob from /sd/ghost/, confirms, then
//  drives the bootloader protocol with a live progress bar.
//
//  This is a SYSTEM app — sits next to ABOUT, FILES, BRIDGE.
//
//  The actual flash work runs on a worker task so the LVGL
//  UI stays responsive (the protocol round-trip can stall for
//  hundreds of ms per block).
// ============================================================

#include "pm_app_c6_flasher.h"
#include "pm_hal.h"
#include "pm_ui.h"
#include "pm_c6_programmer.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>

static const char* TAG = "PM_C6FLASH";

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
static lv_obj_t* s_screen      = NULL;
static lv_obj_t* s_file_list   = NULL;
static lv_obj_t* s_status_lbl  = NULL;
static lv_obj_t* s_progress    = NULL;
static lv_obj_t* s_btn_flash   = NULL;

static char      s_selected_path[128] = "";
static volatile bool s_flashing       = false;
static TaskHandle_t  s_worker_task   = NULL;

// Latest progress shared between worker task and UI tick.
static volatile pm_c6prog_phase_t s_phase     = PM_C6PROG_PHASE_DONE;
static volatile uint32_t          s_bytes_done  = 0;
static volatile uint32_t          s_bytes_total = 0;
static char                       s_msg[64]     = "Idle";

// ─────────────────────────────────────────────
//  Progress callback (runs on worker task)
// ─────────────────────────────────────────────
static void _on_progress(pm_c6prog_phase_t phase, uint32_t done,
                          uint32_t total, const char* message,
                          void* user) {
    (void)user;
    s_phase       = phase;
    s_bytes_done  = done;
    s_bytes_total = total;
    if (message) {
        strncpy(s_msg, message, sizeof(s_msg) - 1);
        s_msg[sizeof(s_msg) - 1] = 0;
    }
}

// ─────────────────────────────────────────────
//  Worker task
// ─────────────────────────────────────────────
static void _flash_task(void* arg) {
    (void)arg;
    pm_log_i(TAG, "Flashing %s …", s_selected_path);
    pm_c6prog_status_t rc = pm_c6_programmer_flash_file(
        s_selected_path, 0x10000, true, _on_progress, NULL);
    pm_log_i(TAG, "Flash done: %s", pm_c6_programmer_status_str(rc));
    s_flashing = false;
    s_worker_task = NULL;
    vTaskDelete(NULL);
}

// ─────────────────────────────────────────────
//  File list population
// ─────────────────────────────────────────────
static void _list_row_cb(lv_event_t* e) {
    const char* path = (const char*)lv_event_get_user_data(e);
    if (!path) return;
    strncpy(s_selected_path, path, sizeof(s_selected_path) - 1);
    s_selected_path[sizeof(s_selected_path) - 1] = 0;
    if (s_btn_flash) lv_obj_clear_state(s_btn_flash, LV_STATE_DISABLED);
    char buf[128];
    snprintf(buf, sizeof(buf), "Selected: %s", s_selected_path);
    if (s_status_lbl) lv_label_set_text(s_status_lbl, buf);
}

static void _populate_file_list(void) {
    if (!s_file_list) return;
    lv_obj_clean(s_file_list);

    DIR* dir = opendir("/sd/ghost");
    if (!dir) {
        lv_obj_t* row = lv_list_add_text(s_file_list,
            "(no /sd/ghost dir — create it and place .bin files there)");
        (void)row;
        return;
    }
    int found = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        const char* name = ent->d_name;
        size_t nlen = strlen(name);
        if (nlen < 4 || strcmp(name + nlen - 4, ".bin") != 0) continue;
        // alloc full path; we'll free implicitly when the screen is destroyed
        // (LVGL doesn't track userdata lifetime, so we lean on PSRAM forever-leak
        // for this short-lived screen instance — acceptable for a system app)
        char* full = (char*)pm_psram_alloc(160);
        if (!full) continue;
        snprintf(full, 160, "/sd/ghost/%s", name);
        lv_obj_t* btn = lv_list_add_button(s_file_list, LV_SYMBOL_FILE, name);
        lv_obj_add_event_cb(btn, _list_row_cb, LV_EVENT_CLICKED, (void*)full);
        found++;
    }
    closedir(dir);

    if (found == 0) {
        lv_list_add_text(s_file_list,
            "(no .bin files found — copy your Ghost firmware to /sd/ghost/)");
    }
}

// ─────────────────────────────────────────────
//  Flash button
// ─────────────────────────────────────────────
static void _flash_clicked(lv_event_t* e) {
    (void)e;
    if (s_flashing) return;
    if (!s_selected_path[0]) return;
    s_flashing = true;
    s_phase     = PM_C6PROG_PHASE_RESET;
    s_bytes_done = 0;
    pm_c6_programmer_init();
    xTaskCreatePinnedToCore(_flash_task, "c6_flash", 8192, NULL, 5,
                              &s_worker_task, 0);
}

// ─────────────────────────────────────────────
//  UI tick: poll worker progress, update bar
// ─────────────────────────────────────────────
static const char* _phase_name(pm_c6prog_phase_t p) {
    switch (p) {
        case PM_C6PROG_PHASE_RESET:  return "RESET";
        case PM_C6PROG_PHASE_SYNC:   return "SYNC";
        case PM_C6PROG_PHASE_ERASE:  return "ERASE";
        case PM_C6PROG_PHASE_WRITE:  return "WRITE";
        case PM_C6PROG_PHASE_VERIFY: return "VERIFY";
        case PM_C6PROG_PHASE_REBOOT: return "REBOOT";
        case PM_C6PROG_PHASE_DONE:   return "DONE";
        case PM_C6PROG_PHASE_FAILED: return "FAILED";
        default:                       return "?";
    }
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    if (!s_progress || !s_status_lbl) return;
    int pct = 0;
    if (s_bytes_total > 0)
        pct = (int)((s_bytes_done * 100) / s_bytes_total);
    lv_bar_set_value(s_progress, pct, LV_ANIM_OFF);
    char buf[96];
    snprintf(buf, sizeof(buf), "[%s] %u/%u  %s",
             _phase_name(s_phase),
             (unsigned)s_bytes_done,
             (unsigned)s_bytes_total,
             s_msg);
    lv_label_set_text(s_status_lbl, buf);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "C6 GHOST FLASHER", NULL, NULL);

    // Header card
    lv_obj_t* hdr = pm_ui_card(s_screen);
    lv_obj_set_height(hdr, 90);
    lv_obj_t* desc = lv_label_create(hdr);
    lv_label_set_text(desc,
        "Place Ghost Engine firmware (.bin) under /sd/ghost/.\n"
        "Selecting a file and pressing FLASH will reboot the C6\n"
        "and overwrite its application image.");
    lv_obj_set_style_text_color(desc, PM_C_FG_DIM, 0);

    // File list
    s_file_list = pm_ui_list(s_screen);
    lv_obj_set_height(s_file_list, 200);

    // Action row
    lv_obj_t* btn_row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 8, 0);
    s_btn_flash = pm_ui_button(btn_row, "FLASH",  _flash_clicked, NULL);
    lv_obj_add_state(s_btn_flash, LV_STATE_DISABLED);

    // Status + progress
    lv_obj_t* status_card = pm_ui_card(s_screen);
    lv_obj_set_flex_grow(status_card, 1);
    s_status_lbl = lv_label_create(status_card);
    lv_label_set_text(s_status_lbl, "Idle. Pick a firmware to flash.");
    lv_obj_set_style_text_color(s_status_lbl, PM_C_FG, 0);

    s_progress = pm_ui_meter_bar(status_card, 0, 100);
}

static void _init(void) { _build_screen(); }

static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    _populate_file_list();
    pm_log_i(TAG, "enter");
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "c6_flasher",
    .display_name = "C6 FLASH",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_c6_flasher(void) { return &_APP; }
