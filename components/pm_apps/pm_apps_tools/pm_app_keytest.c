// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#include "pm_app_keytest.h"

#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "pm_hal.h"
#include "pm_input.h"
#include "pm_ui.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_KEYTEST";

#define KEYTEST_TEXT_CAP 2048
#define KEYTEST_QUEUE_CAP 32

static lv_obj_t* s_screen = NULL;
static lv_obj_t* s_ta = NULL;
static lv_obj_t* s_source_lbl = NULL;
static lv_obj_t* s_last_lbl = NULL;
static lv_obj_t* s_count_lbl = NULL;
static pm_ui_keyboard_t* s_kb = NULL;

static SemaphoreHandle_t s_key_mutex = NULL;
static pm_input_event_t s_queue[KEYTEST_QUEUE_CAP];
static uint8_t s_q_head = 0;
static uint8_t s_q_tail = 0;
static uint32_t s_seen = 0;
static uint32_t s_dropped = 0;
static int s_sub_token = -1;

static const char* _source_name(pm_input_source_t src) {
    switch (src) {
    case PM_INPUT_SRC_VIRTUAL_KEYBOARD: return "screen";
    case PM_INPUT_SRC_VIRTUAL_GAMEPAD: return "gamepad";
    case PM_INPUT_SRC_BT_GAMEPAD: return "bt gamepad";
    case PM_INPUT_SRC_BT_KEYBOARD: return "bt keyboard";
    case PM_INPUT_SRC_I2C_KEYBOARD: return "cardputer i2c";
    default: return "unknown";
    }
}

static const char* _key_name(uint32_t code, char* buf, size_t len) {
    switch (code) {
    case PM_KEY_BACKSPACE: return "BACKSPACE";
    case PM_KEY_TAB: return "TAB";
    case PM_KEY_ENTER: return "ENTER";
    case PM_KEY_ESC: return "ESC";
    case PM_KEY_SPACE: return "SPACE";
    case PM_KEY_DELETE: return "DELETE";
    case PM_KEY_UP: return "UP";
    case PM_KEY_DOWN: return "DOWN";
    case PM_KEY_LEFT: return "LEFT";
    case PM_KEY_RIGHT: return "RIGHT";
    case PM_KEY_HOME: return "HOME";
    case PM_KEY_END: return "END";
    default:
        if (code >= 32 && code <= 126) {
            snprintf(buf, len, "%c", (char)code);
        } else {
            snprintf(buf, len, "0x%lx", (unsigned long)code);
        }
        return buf;
    }
}

static bool _ensure_mutex(void) {
    if (s_key_mutex) return true;
    s_key_mutex = xSemaphoreCreateMutex();
    if (!s_key_mutex) {
        pm_log_w(TAG, "key queue mutex create failed");
        return false;
    }
    return true;
}

static void _on_input(const pm_input_event_t* e, void* user) {
    (void)user;
    if (!e || e->kind != PM_INPUT_KEY || !e->down) return;
    if (!_ensure_mutex()) return;
    if (xSemaphoreTake(s_key_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        s_dropped++;
        return;
    }

    uint8_t next = (uint8_t)((s_q_head + 1) % KEYTEST_QUEUE_CAP);
    if (next == s_q_tail) {
        s_dropped++;
    } else {
        s_queue[s_q_head] = *e;
        s_q_head = next;
    }

    xSemaphoreGive(s_key_mutex);
}

static void _apply_key(const pm_input_event_t* e) {
    if (!e || !s_ta) return;

    switch (e->code) {
    case PM_KEY_BACKSPACE:
    case PM_KEY_DELETE:
        lv_textarea_delete_char(s_ta);
        break;
    case PM_KEY_ENTER:
        lv_textarea_add_char(s_ta, '\n');
        break;
    case PM_KEY_TAB:
        lv_textarea_add_text(s_ta, "    ");
        break;
    case PM_KEY_LEFT:
        lv_textarea_cursor_left(s_ta);
        break;
    case PM_KEY_RIGHT:
        lv_textarea_cursor_right(s_ta);
        break;
    case PM_KEY_UP:
        lv_textarea_cursor_up(s_ta);
        break;
    case PM_KEY_DOWN:
        lv_textarea_cursor_down(s_ta);
        break;
    case PM_KEY_HOME:
        lv_textarea_set_cursor_pos(s_ta, 0);
        break;
    case PM_KEY_END:
        lv_textarea_set_cursor_pos(s_ta, LV_TEXTAREA_CURSOR_LAST);
        break;
    default:
        if (e->code >= 32 && e->code <= 126) {
            lv_textarea_add_char(s_ta, e->code);
        }
        break;
    }

    char key_buf[20];
    char line[96];
    snprintf(line, sizeof(line), "%s  code=%lu",
             _key_name(e->code, key_buf, sizeof(key_buf)),
             (unsigned long)e->code);
    if (s_last_lbl) lv_label_set_text(s_last_lbl, line);
    if (s_source_lbl) lv_label_set_text(s_source_lbl, _source_name(e->source));
    s_seen++;
}

static void _flush_keys(void) {
    pm_input_event_t local[KEYTEST_QUEUE_CAP];
    uint8_t n = 0;

    if (!_ensure_mutex()) return;
    if (xSemaphoreTake(s_key_mutex, 0) != pdTRUE) return;
    while (s_q_tail != s_q_head && n < KEYTEST_QUEUE_CAP) {
        local[n++] = s_queue[s_q_tail];
        s_q_tail = (uint8_t)((s_q_tail + 1) % KEYTEST_QUEUE_CAP);
    }
    xSemaphoreGive(s_key_mutex);

    if (n == 0) return;
    if (!lvgl_port_lock(0)) return;
    for (uint8_t i = 0; i < n; i++) {
        _apply_key(&local[i]);
    }
    if (s_count_lbl) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%lu keys / %lu dropped",
                 (unsigned long)s_seen,
                 (unsigned long)s_dropped);
        lv_label_set_text(s_count_lbl, buf);
    }
    lvgl_port_unlock();
}

static void _clear_clicked(lv_event_t* e) {
    (void)e;
    if (s_ta) lv_textarea_set_text(s_ta, "");
    s_seen = 0;
    s_dropped = 0;
    if (s_count_lbl) lv_label_set_text(s_count_lbl, "0 keys / 0 dropped");
    if (s_last_lbl) lv_label_set_text(s_last_lbl, "-");
}

static void _ta_focused(lv_event_t* e) {
    (void)e;
    pm_ui_keyboard_show(s_kb);
}

static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "KEY TEST", NULL, NULL);

    lv_obj_t* stats = pm_ui_card(s_screen);
    lv_obj_set_height(stats, LV_SIZE_CONTENT);
    s_source_lbl = pm_ui_kv_row(stats, "Source", "-");
    s_last_lbl = pm_ui_kv_row(stats, "Last key", "-");
    s_count_lbl = pm_ui_kv_row(stats, "Count", "0 keys / 0 dropped");

    s_ta = lv_textarea_create(s_screen);
    lv_obj_set_width(s_ta, LV_PCT(100));
    lv_obj_set_flex_grow(s_ta, 1);
    lv_textarea_set_placeholder_text(s_ta, "Type here");
    lv_textarea_set_max_length(s_ta, KEYTEST_TEXT_CAP - 1);
    lv_obj_set_style_bg_color(s_ta, PM_C_BG_2, 0);
    lv_obj_set_style_text_color(s_ta, PM_C_FG, 0);
    lv_obj_set_style_border_color(s_ta, PM_C_BORDER, 0);
    lv_obj_add_event_cb(s_ta, _ta_focused, LV_EVENT_FOCUSED, NULL);

    lv_obj_t* row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);
    pm_ui_button(row, "CLEAR", _clear_clicked, NULL);

    s_kb = pm_ui_keyboard_create(s_screen);
    pm_ui_keyboard_attach(s_kb, s_ta);
}

static void _init(void) {
    _ensure_mutex();
    _build_screen();
}

static void _enter(void) {
    if (s_screen) lv_screen_load(s_screen);
    if (s_sub_token < 0) {
        s_sub_token = pm_input_subscribe(_on_input, NULL);
    }
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    _flush_keys();
}

static void _exit_(void) {
    if (s_sub_token >= 0) {
        pm_input_unsubscribe(s_sub_token);
        s_sub_token = -1;
    }
}

static const pm_app_t _APP = {
    .id           = "keytest",
    .display_name = "KEY TEST",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_keytest(void) { return &_APP; }
