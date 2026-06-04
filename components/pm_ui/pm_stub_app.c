// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

// ============================================================
//  pm_stub_app.c — Shared "coming soon" screen builder
//
//  Each call returns a fully-independent screen with the
//  cyberpunk PCB look, a rainbow title, the genre tagline,
//  description text, and a "PORT IN PROGRESS" status chip.
//  The screen plugs into the launcher's standard back-button
//  flow via pm_app_layout.
//
//  No animations, no per-frame work — the screen is static
//  once built, so registering 12 stubs doesn't pay a runtime
//  cost.
// ============================================================

#include "pm_stub_app.h"
#include "pm_ui.h"
#include "pm_app_layout.h"
#include "pm_cyber.h"
#include "pm_board.h"
#include <string.h>
#include <stdio.h>

lv_obj_t* pm_stub_app_make_screen(const char* title,
                                    const char* tagline,
                                    const char* description) {
    if (!title)        title       = "GAME";
    if (!tagline)      tagline     = "";
    if (!description)  description = "";

    pm_app_layout_t L = {0};
    pm_app_layout_begin(&L, "GAMES");

    pm_app_layout_chip(&L, "PORT IN PROGRESS", PM_LAYOUT_COL_WARN);

    pm_app_layout_content(&L);

    // Single full-width pane that hosts our centered block.
    lv_obj_t* pane = pm_app_layout_pane(&L, 0, NULL);
    lv_obj_set_style_pad_all(pane, 0, 0);

    // PCB background fills the pane.
#if PM_BOARD_LCD_H_RES <= 800
    int pane_w = 800;
    int pane_h = 320;
#else
    int pane_w = 1024;
    int pane_h = 380;
#endif
    pm_cyber_draw_pcb_bg(pane, pane_w, pane_h);

    // Centered block — column with rainbow title, tagline,
    // chip icon, description.
    lv_obj_t* block = lv_obj_create(pane);
    lv_obj_remove_style_all(block);
    lv_obj_set_width(block, LV_PCT(80));
    lv_obj_set_height(block, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(block, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(block, 14, 0);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(block);

    // Rainbow title in big font.
#if PM_BOARD_LCD_H_RES <= 800
    const lv_font_t* title_font = &lv_font_montserrat_28;
#else
    const lv_font_t* title_font = &lv_font_montserrat_48;
#endif
    pm_cyber_rainbow_label(block, title, title_font, 0);

    // Tagline in matrix green.
    if (tagline[0]) {
        lv_obj_t* tl = lv_label_create(block);
        lv_label_set_text(tl, tagline);
        lv_obj_set_style_text_font(tl, PM_LAYOUT_FONT_TEXT, 0);
        lv_obj_set_style_text_color(tl, PM_CYBER_C_MATRIX, 0);
        lv_obj_set_style_text_letter_space(tl, 2, 0);
    }

    // DIP chip icon as a visual breather between title and prose.
#if PM_BOARD_LCD_H_RES <= 800
    int chip_scale = 2;
    int chip_box_h = 56;
#else
    int chip_scale = 3;
    int chip_box_h = 80;
#endif
    lv_obj_t* chip_box = lv_obj_create(block);
    lv_obj_remove_style_all(chip_box);
    lv_obj_set_size(chip_box, 200, chip_box_h);
    lv_obj_set_style_bg_opa(chip_box, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(chip_box, LV_OBJ_FLAG_SCROLLABLE);
    pm_cyber_draw_chip_icon(chip_box, 100, chip_box_h / 2, chip_scale);

    // Description — wrapped, dim color.
    if (description[0]) {
        lv_obj_t* dl = lv_label_create(block);
        lv_label_set_text(dl, description);
        lv_label_set_long_mode(dl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(dl, LV_PCT(90));
        lv_obj_set_style_text_font(dl, PM_LAYOUT_FONT_TEXT, 0);
        lv_obj_set_style_text_color(dl, PM_LAYOUT_COL_FG_DIM, 0);
        lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(dl, 4, 0);
    }

    // Footer hint.
    lv_obj_t* hint = lv_label_create(block);
    lv_label_set_text(hint,
        "Tile reserved in the launcher. Full implementation pending.");
    lv_obj_set_style_text_font(hint, PM_LAYOUT_FONT_LABEL, 0);
    lv_obj_set_style_text_color(hint, PM_LAYOUT_COL_DIM, 0);
    lv_obj_set_style_text_letter_space(hint, 1, 0);

    return pm_app_layout_end(&L);
}
