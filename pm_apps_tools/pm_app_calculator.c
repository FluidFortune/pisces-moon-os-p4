// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_calculator.c — Calculator
//
//  Two-mode design:
//    STD      — basic infix evaluator (no precedence; matches
//               original S3 behavior). Single op accumulator.
//    SUBNET   — CIDR helpers: given a CIDR like 192.168.1.0/24,
//               compute network, broadcast, host count, mask.
//
//  Buttons are wired by the LVGL UI. The math here is the
//  evaluator; the dispatch is _press_*().
// ============================================================

#include "pm_app_calculator.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char* TAG = "PM_CALC";

typedef enum { MODE_STD, MODE_SUBNET } calc_mode_t;

// ── STD state ───────────────────────────────
#define INPUT_MAX 32
static char        s_input[INPUT_MAX] = "0";
static int         s_input_len = 1;
static double      s_accum    = 0.0;
static char        s_op       = ' ';
static calc_mode_t s_mode     = MODE_STD;

// ── SUBNET state ───────────────────────────
static char s_cidr_input[40] = "192.168.1.0/24";
static char s_subnet_result[160] = "";

// LVGL
static void* s_screen     = NULL;
static void* s_lbl_input  = NULL;
static void* s_lbl_result = NULL;

// ─────────────────────────────────────────────
//  STD evaluator
// ─────────────────────────────────────────────
static void _input_clear(void) {
    s_input[0] = '0'; s_input[1] = 0; s_input_len = 1;
    s_accum = 0.0; s_op = ' ';
}

static void _input_append(char c) {
    if (s_input_len >= INPUT_MAX - 1) return;
    if (s_input_len == 1 && s_input[0] == '0' && c != '.') {
        s_input[0] = c;
        return;
    }
    s_input[s_input_len++] = c;
    s_input[s_input_len]   = 0;
}

static void _apply_op(char op) {
    double rhs = strtod(s_input, NULL);
    if (s_op == ' ') {
        s_accum = rhs;
    } else {
        switch (s_op) {
        case '+': s_accum += rhs; break;
        case '-': s_accum -= rhs; break;
        case '*': s_accum *= rhs; break;
        case '/': if (rhs != 0) s_accum /= rhs; break;
        }
    }
    s_op = op;
    if (op != '=') {
        s_input[0] = '0'; s_input[1] = 0; s_input_len = 1;
    } else {
        // Format result back into input
        if (s_accum == (long)s_accum) {
            s_input_len = snprintf(s_input, sizeof(s_input), "%ld", (long)s_accum);
        } else {
            s_input_len = snprintf(s_input, sizeof(s_input), "%g", s_accum);
        }
        s_op = ' ';
    }
}

void pm_app_calc_press_digit(char c) {
    if (s_mode != MODE_STD) return;
    if (isdigit((unsigned char)c) || c == '.') _input_append(c);
}

void pm_app_calc_press_op(char op) {
    if (s_mode != MODE_STD) return;
    _apply_op(op);
}

void pm_app_calc_press_clear(void) {
    _input_clear();
}

const char* pm_app_calc_input(void)  { return s_input; }
double      pm_app_calc_accum(void)  { return s_accum; }

// ─────────────────────────────────────────────
//  SUBNET helpers
// ─────────────────────────────────────────────
// Parse "a.b.c.d/p" → ip and prefix length. Returns true on success.
static bool _parse_cidr(const char* s, uint32_t* out_ip, int* out_prefix) {
    int a, b, c, d, p;
    if (sscanf(s, "%d.%d.%d.%d/%d", &a, &b, &c, &d, &p) != 5) return false;
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255 ||
        p < 0 || p > 32) return false;
    *out_ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
              ((uint32_t)c << 8)  | (uint32_t)d;
    *out_prefix = p;
    return true;
}

static void _ip_to_str(uint32_t ip, char* out, size_t cap) {
    snprintf(out, cap, "%u.%u.%u.%u",
             (unsigned)((ip >> 24) & 0xFF),
             (unsigned)((ip >> 16) & 0xFF),
             (unsigned)((ip >> 8)  & 0xFF),
             (unsigned)(ip         & 0xFF));
}

void pm_app_calc_subnet_calculate(void) {
    uint32_t ip;
    int      prefix;
    if (!_parse_cidr(s_cidr_input, &ip, &prefix)) {
        snprintf(s_subnet_result, sizeof(s_subnet_result),
                 "Bad input. Format: a.b.c.d/p");
        // TODO_LVGL: lv_label_set_text(s_lbl_result, s_subnet_result);
        return;
    }

    uint32_t mask    = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
    uint32_t network = ip & mask;
    uint32_t bcast   = network | (~mask);
    uint64_t hosts   = prefix >= 31 ? 0 : ((1ULL << (32 - prefix)) - 2);

    char ip_s[20], net_s[20], bcast_s[20], mask_s[20];
    _ip_to_str(ip,      ip_s,    sizeof(ip_s));
    _ip_to_str(network, net_s,   sizeof(net_s));
    _ip_to_str(bcast,   bcast_s, sizeof(bcast_s));
    _ip_to_str(mask,    mask_s,  sizeof(mask_s));

    snprintf(s_subnet_result, sizeof(s_subnet_result),
             "IP:        %s/%d\n"
             "Network:   %s\n"
             "Broadcast: %s\n"
             "Mask:      %s\n"
             "Hosts:     %llu",
             ip_s, prefix, net_s, bcast_s, mask_s,
             (unsigned long long)hosts);
    pm_log_i(TAG, "subnet: %s/%d -> %s..%s, %llu hosts",
             net_s, prefix, net_s, bcast_s, (unsigned long long)hosts);
}

void pm_app_calc_subnet_set_input(const char* s) {
    if (!s) return;
    strncpy(s_cidr_input, s, sizeof(s_cidr_input) - 1);
    s_cidr_input[sizeof(s_cidr_input) - 1] = 0;
}

// ─────────────────────────────────────────────
//  Mode toggle
// ─────────────────────────────────────────────
void pm_app_calc_toggle_mode(void) {
    s_mode = (s_mode == MODE_STD) ? MODE_SUBNET : MODE_STD;
    pm_log_i(TAG, "mode: %s", s_mode == MODE_STD ? "STD" : "SUBNET");
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_calc_screen   = NULL;
static lv_obj_t* s_lbl_display   = NULL;
static lv_obj_t* s_lbl_subnet    = NULL;

static void _refresh_display(void) {
    if (s_lbl_display) lv_label_set_text(s_lbl_display, s_input);
    if (s_lbl_subnet)  lv_label_set_text(s_lbl_subnet,  s_subnet_result);
}

static void _on_key(char k, void* user) {
    (void)user;
    switch (k) {
        case 'C': pm_app_calc_press_clear(); break;
        case 'M': pm_app_calc_toggle_mode(); break;
        case '=': pm_app_calc_press_op('='); break;
        case '+': case '-': case '*': case '/':
            pm_app_calc_press_op(k); break;
        default:
            if (isdigit((unsigned char)k) || k == '.')
                pm_app_calc_press_digit(k);
            break;
    }
    _refresh_display();
}

static void _build_screen(void) {
    s_calc_screen = pm_ui_screen();
    pm_ui_titlebar(s_calc_screen, "CALC", NULL, NULL);

    // Display row
    lv_obj_t* disp_card = pm_ui_card(s_calc_screen);
    lv_obj_set_height(disp_card, 80);
    s_lbl_display = lv_label_create(disp_card);
    lv_label_set_text(s_lbl_display, s_input);
    lv_obj_set_style_text_color(s_lbl_display, PM_C_ACCENT, 0);
    lv_obj_set_style_text_font (s_lbl_display, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(s_lbl_display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_lbl_display, LV_PCT(100));

    s_lbl_subnet = lv_label_create(disp_card);
    lv_label_set_text(s_lbl_subnet, "");
    lv_obj_set_style_text_color(s_lbl_subnet, PM_C_FG_DIM, 0);

    // Keypad
    lv_obj_t* pad = pm_ui_keypad(s_calc_screen,
        "C/*M\n789-\n456+\n123\n.0=", _on_key, NULL);
    lv_obj_set_flex_grow(pad, 1);
}

static void _init(void)  { _build_screen(); _input_clear(); }
static void _enter(void) {
    if (s_calc_screen) lv_screen_load(s_calc_screen);
    _refresh_display();
    pm_log_i(TAG, "enter");
}
static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "calculator",
    .display_name = "CALC",
    .category     = PM_CAT_TOOLS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_calculator(void) { return &_APP; }
