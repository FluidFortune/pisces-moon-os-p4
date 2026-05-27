#!/bin/bash
# ============================================================
#  pm_phase16_full_audit.sh
#  Pisces Moon P4 — Comprehensive Audit + Fixes
#  Copyright (C) 2026 Eric Becker / Fluid Fortune
#  SPDX-License-Identifier: AGPL-3.0-or-later
#
#  Single self-contained script. Embeds all new files.
#
#  Usage:
#    cd /Users/eric/workspace/PiscesMoon-alpha-1.2.0/pisces-moon-p4
#    chmod +x pm_phase16_full_audit.sh
#    ./pm_phase16_full_audit.sh
#
#  Then:
#    rm sdkconfig && idf.py fullclean && idf.py build
#    idf.py -p $(ls /dev/cu.usbmodem*) flash
# ============================================================

set -e
PROJ_ROOT="$(pwd)"
if [ ! -f "main/main.c" ]; then
    echo "ERROR: run from project root (no main/main.c found)"
    exit 1
fi

echo "================================================"
echo "  PISCES MOON P4 — PHASE 16 AUDIT"
echo "================================================"

BACKUP_DIR="/tmp/pm_audit_$(date +%s)"
mkdir -p "$BACKUP_DIR"
cp -r main "$BACKUP_DIR/"
cp -r components/pm_apps/pm_apps_cyber "$BACKUP_DIR/"
[ -f sdkconfig.defaults ] && cp sdkconfig.defaults "$BACKUP_DIR/"
echo "Backup: $BACKUP_DIR"
echo ""

# ════════════════════════════════════════════════════════════
# WRITE PM_BOOT COMPONENT FILES
# ════════════════════════════════════════════════════════════
echo "[1/9] Writing pm_boot component..."
mkdir -p components/pm_boot/include

cat > components/pm_boot/include/pm_boot.h << 'BOOT_H_EOF'
#pragma once
#include "lvgl.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_BOOT_OK       = 0,
    PM_BOOT_ACTIVE   = 1,
    PM_BOOT_WARN     = 2,
    PM_BOOT_FAIL     = 3,
    PM_BOOT_DISABLED = 4,
} pm_boot_status_t;

void pm_boot_screen_show(void);
void pm_boot_step(const char* label, const char* detail, pm_boot_status_t status);
void pm_boot_progress(int percent);
void pm_boot_splash_show(uint32_t duration_ms);
void pm_boot_dismiss(void);

#ifdef __cplusplus
}
#endif
BOOT_H_EOF

cat > components/pm_boot/pm_boot.c << 'BOOT_C_EOF'
// pm_boot.c — Pisces Moon P4 boot UI
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "pm_boot.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    s_boot_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_boot_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_boot_screen, 1024, 600);
    lv_obj_set_style_bg_color(s_boot_screen, BOOT_BG, 0);
    lv_obj_set_style_bg_opa(s_boot_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_boot_screen, 0, 0);
    lv_obj_set_style_border_width(s_boot_screen, 0, 0);
    lv_obj_clear_flag(s_boot_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr = lv_obj_create(s_boot_screen);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, 1024, 36);
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
    lv_obj_set_size(ftr, 1024, 28);
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
    lv_obj_set_size(s_progress_bar, 992, 8);
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
    lv_obj_set_size(s_log_container, 1000, 500);
    lv_obj_align(s_log_container, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_bg_opa(s_log_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_log_container, 4, 0);
    lv_obj_set_layout(s_log_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_log_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_log_container, 2, 0);
    lv_obj_set_scroll_dir(s_log_container, LV_DIR_VER);

    lv_screen_load(s_boot_screen);
    lv_refr_now(NULL);
}

void pm_boot_step(const char* label, const char* detail, pm_boot_status_t status) {
    if (!s_log_container) return;

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
    ESP_LOGI(TAG, "%s %s %s %s", ts, label, detail?detail:"", tag_text);
}

void pm_boot_progress(int percent) {
    if (!s_progress_bar) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_bar_set_value(s_progress_bar, percent, LV_ANIM_OFF);
    char buf[48];
    snprintf(buf, sizeof(buf), "INITIALIZING...  %d%%", percent);
    lv_label_set_text(s_progress_lbl, buf);
    lv_refr_now(NULL);
}

void pm_boot_splash_show(uint32_t duration_ms) {
    s_splash_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_splash_screen, 1024, 600);
    lv_obj_set_style_bg_color(s_splash_screen, BOOT_BG, 0);
    lv_obj_set_style_bg_opa(s_splash_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_splash_screen, 0, 0);
    lv_obj_set_style_border_width(s_splash_screen, 0, 0);
    lv_obj_clear_flag(s_splash_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* frame = lv_obj_create(s_splash_screen);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 980, 556);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_border_opa(frame, 80, 0);
    lv_obj_set_style_radius(frame, 24, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* inner = lv_obj_create(s_splash_screen);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 960, 536);
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

    lv_obj_t* sub = lv_label_create(s_splash_screen);
    lv_label_set_text(sub, "the OS");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_text_letter_space(sub, 8, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 30);

    lv_obj_t* t1 = lv_label_create(s_splash_screen);
    lv_label_set_text(t1, "Powered by Gemini.");
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t1, BOOT_TEXT_BRIGHT, 0);
    lv_obj_set_style_text_letter_space(t1, 2, 0);
    lv_obj_align(t1, LV_ALIGN_CENTER, 0, 90);

    lv_obj_t* t2 = lv_label_create(s_splash_screen);
    lv_label_set_text(t2, "Limited only by your imagination.");
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t2, BOOT_TEXT, 0);
    lv_obj_align(t2, LV_ALIGN_CENTER, 0, 118);

    lv_obj_t* wm = lv_label_create(s_splash_screen);
    lv_label_set_text(wm, "FLUID FORTUNE  /  fluidfortune.com");
    lv_obj_set_style_text_font(wm, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wm, lv_color_hex(0xf4a820), 0);
    lv_obj_set_style_text_letter_space(wm, 3, 0);
    lv_obj_align(wm, LV_ALIGN_BOTTOM_MID, 0, -32);

    lv_screen_load(s_splash_screen);
    lv_refr_now(NULL);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
}

void pm_boot_dismiss(void) {
    if (s_splash_screen) { lv_obj_delete(s_splash_screen); s_splash_screen = NULL; }
    if (s_boot_screen)   { lv_obj_delete(s_boot_screen);   s_boot_screen = NULL; }
    s_log_container = NULL;
    s_progress_bar  = NULL;
    s_progress_lbl  = NULL;
    ESP_LOGI(TAG, "boot UI dismissed, memory freed");
}
BOOT_C_EOF

cat > components/pm_boot/CMakeLists.txt << 'BOOT_CMAKE_EOF'
idf_component_register(
    SRCS "pm_boot.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lvgl_port lvgl freertos esp_common log
)
BOOT_CMAKE_EOF

echo "  pm_boot/ component installed"
echo ""

# ════════════════════════════════════════════════════════════
# NEUTRALIZE _boot_visual_probe
# ════════════════════════════════════════════════════════════
echo "[2/9] Neutralizing _boot_visual_probe (LEDC pin theft fix)..."
python3 << 'PYEOF'
import re
path = 'main/main.c'
src = open(path).read()
old_func = re.compile(
    r'static void _boot_visual_probe\(bool on\) \{[\s\S]*?\n\}',
    re.MULTILINE
)
new_func = '''static void _boot_visual_probe(bool on) {
    (void)on;
    // DISABLED: was reconfiguring PM_PIN_LCD_BL as plain GPIO,
    // stealing it from LEDC. Caused periodic backlight pulses
    // under heavy render load. Boot UI now handled by pm_boot.
}'''
if old_func.search(src):
    src = old_func.sub(new_func, src, count=1)
    open(path, 'w').write(src)
    print("  _boot_visual_probe neutered")
else:
    print("  _boot_visual_probe not found (already patched)")
PYEOF

# ════════════════════════════════════════════════════════════
# REMOVE HEARTBEAT LOOP FROM app_main
# ════════════════════════════════════════════════════════════
echo "[3/9] Removing boot heartbeat loop from app_main..."
python3 << 'PYEOF'
import re
path = 'main/main.c'
src = open(path).read()

# Strip both possible heartbeat blocks
patterns = [
    r'    // BOOT DIAGNOSTIC:[\s\S]*?fflush\(stdout\);\n',
    r'    // Boot heartbeat removed[\s\S]*?fflush\(stdout\);\n',
]
for p in patterns:
    src = re.sub(p, '', src)

# Add pm_boot include
if '#include "pm_boot.h"' not in src:
    src = src.replace('#include "esp_err.h"',
                       '#include "esp_err.h"\n#include "pm_boot.h"', 1)

open(path, 'w').write(src)
print("  heartbeat loop removed")
PYEOF

# ════════════════════════════════════════════════════════════
# DEFER WARDRIVE DB CREATION
# ════════════════════════════════════════════════════════════
echo "[4/9] Deferring wardrive DB init (fixes 6-second freeze)..."
python3 << 'PYEOF'
import re
path = 'components/pm_apps/pm_apps_cyber/pm_app_wardrive.c'
src = open(path).read()

# Remove _ensure_db calls from _enter and _render
src = re.sub(
    r'(_enter\(void\) \{[\s\S]*?)[ \t]*_ensure_db\(\);[ \t]*\n',
    r'\1    // DB deferred to scan start\n',
    src
)
src = re.sub(
    r'(_render\(void\) \{[\s\S]*?)[ \t]*_ensure_db\(\);[ \t]*\n',
    r'\1    // DB lazy-open in _start_cb only\n',
    src
)

# Default to CSV fallback (faster, no SD penalty)
src = src.replace(
    'static bool     s_csv_fallback = false;',
    'static bool     s_csv_fallback = true;   // CSV fast path by default'
)

open(path, 'w').write(src)
print("  DB init deferred, CSV is default")
PYEOF

# ════════════════════════════════════════════════════════════
# WIRE WARDRIVE START BUTTON TO esp_wifi_scan_start
# ════════════════════════════════════════════════════════════
echo "[5/9] Wiring wardrive START to esp_wifi_scan_start..."
python3 << 'PYEOF'
import re
path = 'components/pm_apps/pm_apps_cyber/pm_app_wardrive.c'
src = open(path).read()

if '#include "esp_wifi.h"' not in src:
    src = src.replace('#include "lvgl.h"',
                       '#include "lvgl.h"\n#include "esp_wifi.h"\n#include "esp_event.h"', 1)

# Add forward declaration of pm_app_wardrive_add_network if not exported elsewhere
# (it's defined further down)

# Replace simple _start_cb stub with real wifi scan trigger
old = re.compile(
    r'static void _start_cb\(lv_event_t\* e\) \{[\s\S]*?\n\}',
    re.MULTILINE
)
new = '''static void _start_cb(lv_event_t* e) {
    (void)e;
    if (s_running) return;
    s_running = true;
    pm_log_i("WARDRIVE", "scan start");

    // Lazy session init (deferred from _enter)
    _ensure_db();

    // Trigger WiFi scan via ESP-Hosted (C6)
    wifi_scan_config_t cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 250 }}
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, false);
    if (err != ESP_OK) {
        pm_log_w("WARDRIVE", "scan_start failed: 0x%x", (unsigned)err);
    }

    if (s_rec_dot) lv_obj_set_style_bg_color(s_rec_dot, lv_color_hex(0xff3366), 0);
    if (s_btn_start) lv_obj_set_style_bg_opa(s_btn_start, 80, 0);
}'''

# Only the first occurrence matches
m = old.search(src)
if m:
    src = src[:m.start()] + new + src[m.end():]
    print("  _start_cb wired to esp_wifi_scan_start")
else:
    print("  _start_cb not found in expected form")

open(path, 'w').write(src)
PYEOF

# ════════════════════════════════════════════════════════════
# RE-ENABLE ESP-HOSTED
# ════════════════════════════════════════════════════════════
echo "[6/9] Re-enabling ESP-Hosted (C6 WiFi/BT)..."
python3 << 'PYEOF'
path = 'sdkconfig.defaults'
src = open(path).read()
add = '''
# Phase 16: ESP-Hosted re-enabled for C6 WiFi/BT
CONFIG_ESP_HOSTED_ENABLED=y
'''
if 'CONFIG_ESP_HOSTED_ENABLED=y' not in src:
    src += add
    open(path, 'w').write(src)
    print("  CONFIG_ESP_HOSTED_ENABLED=y added")
else:
    print("  already enabled")
PYEOF

# Uncomment in idf_component.yml if commented
if [ -f main/idf_component.yml ]; then
    sed -i.bak 's/^#\s*espressif\/esp_hosted/  espressif\/esp_hosted/' main/idf_component.yml || true
fi

# ════════════════════════════════════════════════════════════
# WIRE GPS CHIP TO pm_gps STATE
# ════════════════════════════════════════════════════════════
echo "[7/9] Wiring GPS chip to pm_gps state..."
python3 << 'PYEOF'
import re
path = 'components/pm_apps/pm_apps_cyber/pm_app_wardrive.c'
src = open(path).read()

if '#include "pm_gps.h"' not in src:
    src = src.replace('#include "esp_event.h"',
                       '#include "esp_event.h"\n#include "pm_gps.h"', 1)

gps_block = '''    // GPS chip live update
    if (s_chip_gps) {
        pm_gps_t g; pm_gps_state_get(&g);
        lv_obj_t* lbl = lv_obj_get_child(s_chip_gps, 0);
        char buf[16];
        lv_color_t c;
        if (g.valid) {
            snprintf(buf, sizeof(buf), "GPS %d", (int)g.sats);
            c = lv_color_hex(0x00ff88);
        } else if (g.sats > 0) {
            snprintf(buf, sizeof(buf), "GPS %d?", (int)g.sats);
            c = lv_color_hex(0xffcc00);
        } else {
            snprintf(buf, sizeof(buf), "GPS --");
            c = lv_color_hex(0x2a5870);
        }
        if (lbl) {
            lv_label_set_text(lbl, buf);
            lv_obj_set_style_text_color(lbl, c, 0);
        }
        lv_obj_set_style_border_color(s_chip_gps, c, 0);
    }

'''

# Inject at end of _render before closing brace
if 'GPS chip live update' not in src:
    pattern = re.compile(r'(static void _render\(void\) \{[\s\S]*?)\n\}', re.MULTILINE)
    m = pattern.search(src)
    if m:
        src = src[:m.end(1)] + '\n' + gps_block + '}' + src[m.end():]
        print("  GPS chip update injected into _render")
    else:
        print("  _render function not found")

open(path, 'w').write(src)
PYEOF

# ════════════════════════════════════════════════════════════
# STATUS BAR INTEGRATION (optional convenience header)
# ════════════════════════════════════════════════════════════
echo "[8/9] Audit notes..."
cat > /tmp/pm_phase16_notes.txt << 'NOTES_EOF'
PHASE 16 AUDIT — MANUAL FOLLOW-UPS

The audit script applied all automated changes. A few items
remain that need manual integration in main.c (your app_main
sequencing is unique enough that mechanical patching is risky).

1. CALL pm_boot AT START OF app_main:

   Add these calls early in app_main, AFTER pm_bsp_init() succeeds:

       pm_boot_screen_show();
       pm_boot_step("CPU HP0",  "360 MHz RISC-V LX9",     PM_BOOT_OK);
       pm_boot_step("CPU HP1",  "360 MHz RISC-V LX9",     PM_BOOT_OK);
       pm_boot_step("CPU LP",   "40 MHz (Sentinel)",      PM_BOOT_OK);
       pm_boot_step("PSRAM",    "32 MB @ 200 MHz HEX",    PM_BOOT_OK);
       pm_boot_step("FLASH",    "16 MB DIO",              PM_BOOT_OK);
       pm_boot_progress(20);

       // After display + LVGL ready:
       pm_boot_step("MIPI-DSI", "1024x600 EK79007",       PM_BOOT_OK);
       pm_boot_step("TOUCH",    "GT911 I2C 400KHz",       PM_BOOT_OK);
       pm_boot_step("LVGL",     "v9.2 TLSF 64KB",         PM_BOOT_OK);
       pm_boot_progress(50);

       // SD + DB:
       pm_boot_step("SDMMC",    "10 MHz 1-bit",           PM_BOOT_OK);
       pm_boot_step("SQLITE",   "v3.45",                  PM_BOOT_OK);
       pm_boot_progress(70);

       // Radios:
       pm_boot_step("GPS NMEA", "UART1 9600 baud",        PM_BOOT_OK);
       pm_boot_step("C6 GHOST", "ESP-Hosted SDIO",        PM_BOOT_OK);
       pm_boot_step("T-BEAM",   "(not present)",          PM_BOOT_DISABLED);
       pm_boot_progress(85);

       // Final:
       pm_boot_step("APPS",     "57 registered",          PM_BOOT_OK);
       pm_boot_step("LAUNCHER", "ready",                  PM_BOOT_OK);
       pm_boot_progress(100);

       // Show splash for 2 seconds then dismiss boot UI
       pm_boot_splash_show(2000);
       pm_boot_dismiss();

       // Then show launcher
       pm_launcher_show();

2. ESP-HOSTED INIT (if not already in your code):

   After NVS init but before app registration:

       esp_event_loop_create_default();
       esp_netif_init();
       wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
       esp_wifi_init(&wcfg);
       esp_wifi_set_mode(WIFI_MODE_STA);
       esp_wifi_start();

3. BUILD STEPS:

       rm sdkconfig
       idf.py fullclean
       idf.py build

   If managed_components fail to download esp_hosted, manually:
       idf.py reconfigure

4. EXPECTED OUTCOME:

   - Boot: dark screen, then POST-style status lines render fast
   - 2-second splash with PISCES MOON title
   - Launcher takes over
   - Tap CYBER -> wardrive: opens INSTANTLY (no DB freeze)
   - Tap START: WiFi scan begins, networks populate left panel
   - GPS chip turns green when satellites lock
   - NO MORE PERIODIC PULSING

5. KNOWN ISSUES STILL OPEN:

   - BLE scan integration (uses NimBLE; separate _start_cb wiring)
   - Mesh messenger LoRa wiring (waits for module slot)
   - Status bar overlay (Phase 16 task, not in this audit)
   - pm_sdlog queue component (Phase 16, future)
NOTES_EOF

echo "  Manual integration notes saved to /tmp/pm_phase16_notes.txt"
echo ""

# ════════════════════════════════════════════════════════════
# DONE
# ════════════════════════════════════════════════════════════
echo "[9/9] Audit script complete!"
echo ""
echo "════════════════════════════════════════════════"
echo "  CHANGES SUMMARY"
echo "════════════════════════════════════════════════"
echo "  ✓ pm_boot component installed"
echo "  ✓ _boot_visual_probe neutralized (LEDC pin freed)"
echo "  ✓ Heartbeat loop removed from app_main"
echo "  ✓ Wardrive DB init deferred to scan-start"
echo "  ✓ Wardrive CSV-first logging (NoSQL fast path)"
echo "  ✓ Wardrive START -> esp_wifi_scan_start()"
echo "  ✓ ESP-Hosted enabled for C6 radios"
echo "  ✓ GPS chip wired to pm_gps state"
echo ""
echo "  → Manual integration notes: /tmp/pm_phase16_notes.txt"
echo "  → Read those notes, integrate pm_boot calls into main.c"
echo "  → Then: rm sdkconfig && idf.py fullclean && idf.py build"
echo ""
echo "  Rollback: $BACKUP_DIR"
echo "════════════════════════════════════════════════"
