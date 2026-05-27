// pm_boot.c — Pisces Moon P4 boot UI
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "pm_boot.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "pm_board.h"

static const char* TAG = "PM_BOOT";

static lv_obj_t* s_boot_screen   = NULL;
static lv_obj_t* s_log_container = NULL;
static lv_obj_t* s_progress_bar  = NULL;
static lv_obj_t* s_progress_lbl  = NULL;
static lv_obj_t* s_splash_screen = NULL;
static uint32_t  s_boot_start_ms = 0;

#define BOOT_BG          lv_color_hex(0x050a0e)
#define BOOT_BG2         lv_color_hex(0x080f14)
#define BOOT_BG3         lv_color_hex(0x0c1620)
#define BOOT_BORDER      lv_color_hex(0x1a3a50)
#define BOOT_HEADER_BG   lv_color_hex(0x0d2030)
#define BOOT_TEXT        lv_color_hex(0x7ab8d4)
#define BOOT_TEXT_BRIGHT lv_color_hex(0xc8e8f5)
#define BOOT_TEXT_DIM    lv_color_hex(0x2a5870)
#define BOOT_TS_COLOR    lv_color_hex(0x4a7a92)
#define BOOT_OK_COLOR    lv_color_hex(0x00ff88)
#define BOOT_ACTIVE_COL  lv_color_hex(0x00d4ff)
#define BOOT_WARN_COLOR  lv_color_hex(0xffcc00)
#define BOOT_FAIL_COLOR  lv_color_hex(0xff3366)
#define BOOT_DISABLED    lv_color_hex(0x4a7a92)

static void _format_ts(char* buf, size_t len) {
    uint32_t ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - s_boot_start_ms;
    snprintf(buf, len, "[%02u.%02u]", (unsigned)(ms/1000), (unsigned)((ms%1000)/10));
}

void pm_boot_screen_show(void) {
    if (!lvgl_port_lock(0)) return;
    s_boot_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_boot_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_boot_screen, PM_BOARD_LCD_H_RES, PM_BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_boot_screen, BOOT_BG, 0);
    lv_obj_set_style_bg_opa(s_boot_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_boot_screen, 0, 0);
    lv_obj_set_style_border_width(s_boot_screen, 0, 0);
    lv_obj_clear_flag(s_boot_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_obj_create(s_boot_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, PM_BOARD_LCD_H_RES, 36);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, BOOT_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr, BOOT_BORDER, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(hdr, 16, 0);

    lv_obj_t* logo = lv_label_create(hdr);
    lv_label_set_text(logo, "PISCES MOON OS");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(logo, BOOT_OK_COLOR, 0);
    lv_obj_set_style_text_letter_space(logo, 3, 0);
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* bios = lv_label_create(hdr);
    lv_label_set_text(bios, "BIOS v1.2.0  /  ESP32-P4  /  RISC-V LX9");
    lv_obj_set_style_text_font(bios, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bios, BOOT_TEXT_DIM, 0);
    lv_obj_align(bios, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* ftr = lv_obj_create(s_boot_screen);
    lv_obj_remove_style_all(ftr);
    lv_obj_set_size(ftr, PM_BOARD_LCD_H_RES, 28);
    lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ftr, BOOT_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(ftr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ftr, BOOT_BORDER, 0);
    lv_obj_set_style_border_width(ftr, 1, 0);
    lv_obj_set_style_border_side(ftr, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_hor(ftr, 16, 0);

    lv_obj_t* fl = lv_label_create(ftr);
    lv_label_set_text(fl, "OS CORE READY / UI CORE ACTIVE");
    lv_obj_set_style_text_font(fl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(fl, BOOT_TEXT_DIM, 0);
    lv_obj_set_style_text_letter_space(fl, 2, 0);
    lv_obj_align(fl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* fr = lv_label_create(ftr);
    lv_label_set_text(fr, "fluidfortune.com");
    lv_obj_set_style_text_font(fr, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(fr, BOOT_TEXT_DIM, 0);
    lv_obj_align(fr, LV_ALIGN_RIGHT_MID, 0, 0);

    s_progress_bar = lv_bar_create(s_boot_screen);
    lv_obj_set_size(s_progress_bar, PM_BOARD_LCD_H_RES - 32, 8);
    lv_obj_align(s_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_obj_set_style_bg_color(s_progress_bar, BOOT_BG2, 0);
    lv_obj_set_style_bg_opa(s_progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_progress_bar, BOOT_BORDER, 0);
    lv_obj_set_style_border_width(s_progress_bar, 1, 0);
    lv_obj_set_style_radius(s_progress_bar, 0, 0);
    lv_obj_set_style_bg_color(s_progress_bar, BOOT_OK_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress_bar, 0, LV_PART_INDICATOR);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);

    s_progress_lbl = lv_label_create(s_boot_screen);
    lv_label_set_text(s_progress_lbl, "INITIALIZING...  0%");
    lv_obj_set_style_text_font(s_progress_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_progress_lbl, BOOT_TEXT_DIM, 0);
    lv_obj_set_style_text_letter_space(s_progress_lbl, 2, 0);
    lv_obj_align(s_progress_lbl, LV_ALIGN_BOTTOM_MID, 0, -58);

    s_log_container = lv_obj_create(s_boot_screen);
    lv_obj_remove_style_all(s_log_container);
    lv_obj_set_size(s_log_container, PM_BOARD_LCD_H_RES - 24, PM_BOARD_LCD_V_RES - 100);
    lv_obj_align(s_log_container, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_bg_opa(s_log_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_log_container, 4, 0);
    lv_obj_set_layout(s_log_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_log_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_log_container, 2, 0);
    lv_obj_set_scroll_dir(s_log_container, LV_DIR_VER);

    lv_screen_load(s_boot_screen);
    lv_refr_now(NULL);
    lvgl_port_unlock();
}

void pm_boot_step(const char* label, const char* detail, pm_boot_status_t status) {
    if (!s_log_container) return;
    if (!lvgl_port_lock(0)) return;
    if (!s_log_container) {
        lvgl_port_unlock();
        return;
    }

    lv_obj_t* row = lv_obj_create(s_log_container);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 18);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    char ts[16]; _format_ts(ts, sizeof(ts));
    lv_obj_t* ts_lbl = lv_label_create(row);
    lv_label_set_text(ts_lbl, ts);
    lv_obj_set_style_text_font(ts_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ts_lbl, BOOT_TS_COLOR, 0);
    lv_obj_set_width(ts_lbl, 70);

    lv_obj_t* lbl_lbl = lv_label_create(row);
    lv_label_set_text(lbl_lbl, label);
    lv_obj_set_style_text_font(lbl_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_lbl, BOOT_TEXT_BRIGHT, 0);
    lv_obj_set_style_text_letter_space(lbl_lbl, 1, 0);
    lv_obj_set_width(lbl_lbl, 220);

    lv_obj_t* dtl_lbl = lv_label_create(row);
    lv_label_set_text(dtl_lbl, detail ? detail : "");
    lv_obj_set_style_text_font(dtl_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dtl_lbl, BOOT_TEXT, 0);
    lv_obj_set_flex_grow(dtl_lbl, 1);
    lv_label_set_long_mode(dtl_lbl, LV_LABEL_LONG_CLIP);

    lv_obj_t* tag = lv_obj_create(row);
    lv_obj_remove_style_all(tag);
    lv_obj_set_size(tag, 80, 14);
    lv_obj_set_style_radius(tag, 2, 0);
    lv_obj_set_style_border_width(tag, 1, 0);
    lv_obj_clear_flag(tag, LV_OBJ_FLAG_SCROLLABLE);

    const char* tag_text = "[ OK ]";
    lv_color_t tag_color = BOOT_OK_COLOR;
    switch (status) {
        case PM_BOOT_OK:       tag_text = "[ OK ]";     tag_color = BOOT_OK_COLOR; break;
        case PM_BOOT_ACTIVE:   tag_text = "[ ACTIVE ]"; tag_color = BOOT_ACTIVE_COL; break;
        case PM_BOOT_WARN:     tag_text = "[ WARN ]";   tag_color = BOOT_WARN_COLOR; break;
        case PM_BOOT_FAIL:     tag_text = "[ FAIL ]";   tag_color = BOOT_FAIL_COLOR; break;
        case PM_BOOT_DISABLED: tag_text = "[ -- ]";     tag_color = BOOT_DISABLED; break;
    }
    lv_obj_set_style_bg_color(tag, tag_color, 0);
    lv_obj_set_style_bg_opa(tag, 30, 0);
    lv_obj_set_style_border_color(tag, tag_color, 0);
    lv_obj_t* tag_lbl = lv_label_create(tag);
    lv_label_set_text(tag_lbl, tag_text);
    lv_obj_set_style_text_font(tag_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(tag_lbl, tag_color, 0);
    lv_obj_center(tag_lbl);

    lv_refr_now(NULL);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "%s %s %s %s", ts, label, detail?detail:"", tag_text);
}

void pm_boot_progress(int percent) {
    if (!s_progress_bar) return;
    if (!lvgl_port_lock(0)) return;
    if (!s_progress_bar) {
        lvgl_port_unlock();
        return;
    }
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_bar_set_value(s_progress_bar, percent, LV_ANIM_OFF);
    char buf[48];
    snprintf(buf, sizeof(buf), "INITIALIZING...  %d%%", percent);
    lv_label_set_text(s_progress_lbl, buf);
    lv_refr_now(NULL);
    lvgl_port_unlock();
}

void pm_boot_splash_show(uint32_t duration_ms) {
    if (!lvgl_port_lock(0)) return;
    s_splash_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_splash_screen, PM_BOARD_LCD_H_RES, PM_BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_splash_screen, BOOT_BG, 0);
    lv_obj_set_style_bg_opa(s_splash_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_splash_screen, 0, 0);
    lv_obj_set_style_border_width(s_splash_screen, 0, 0);
    lv_obj_clear_flag(s_splash_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* frame = lv_obj_create(s_splash_screen);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, PM_BOARD_LCD_H_RES - 44, PM_BOARD_LCD_V_RES - 44);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_opa(frame, 80, 0);
    lv_obj_set_style_radius(frame, 24, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* inner = lv_obj_create(s_splash_screen);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, PM_BOARD_LCD_H_RES - 64, PM_BOARD_LCD_V_RES - 64);
    lv_obj_align(inner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_opa(inner, 40, 0);
    lv_obj_set_style_radius(inner, 20, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* chip = lv_obj_create(s_splash_screen);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 96, 96);
    lv_obj_align(chip, LV_ALIGN_CENTER, 0, -140);
    lv_obj_set_style_bg_color(chip, BOOT_BG3, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_border_width(chip, 2, 0);
    lv_obj_set_style_radius(chip, 6, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* chip_lbl = lv_label_create(chip);
    lv_label_set_text(chip_lbl, "P4");
    lv_obj_set_style_text_font(chip_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(chip_lbl, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_letter_space(chip_lbl, 4, 0);
    lv_obj_center(chip_lbl);

    lv_obj_t* title = lv_label_create(s_splash_screen);
    lv_label_set_text(title, "PISCES MOON");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_text_letter_space(title, 12, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t* t1 = lv_label_create(s_splash_screen);
    lv_label_set_text(t1, "Powered by Gemini.");
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t1, BOOT_TEXT_BRIGHT, 0);
    lv_obj_set_style_text_letter_space(t1, 2, 0);
    lv_obj_align(t1, LV_ALIGN_CENTER, 0, 74);

    lv_obj_t* t2 = lv_label_create(s_splash_screen);
    lv_label_set_text(t2, "Limited only by your imagination.");
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t2, BOOT_TEXT, 0);
    lv_obj_align(t2, LV_ALIGN_CENTER, 0, 104);

    lv_obj_t* wm = lv_label_create(s_splash_screen);
    lv_label_set_text(wm, "FLUID FORTUNE  /  fluidfortune.com");
    lv_obj_set_style_text_font(wm, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wm, lv_color_hex(0xf4a820), 0);
    lv_obj_set_style_text_letter_space(wm, 3, 0);
    lv_obj_align(wm, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_screen_load(s_splash_screen);
    lv_refr_now(NULL);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

void pm_boot_dismiss(void) {
    if (!lvgl_port_lock(0)) return;
    lv_obj_t* active = lv_screen_active();
    if (s_splash_screen && s_splash_screen != active) {
        lv_obj_delete(s_splash_screen);
        s_splash_screen = NULL;
    }
    if (s_boot_screen && s_boot_screen != active) {
        lv_obj_delete(s_boot_screen);
        s_boot_screen = NULL;
    }
    s_log_container = NULL;
    s_progress_bar  = NULL;
    s_progress_lbl  = NULL;
    lvgl_port_unlock();
    ESP_LOGI(TAG, "boot UI dismissed, memory freed");
}
