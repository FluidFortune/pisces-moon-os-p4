// ============================================================
//  pm_app_wardrive.c — WARDRIVE app, Pisces Moon P4
//  Copyright (C) 2026 Eric Becker / Fluid Fortune
//  SPDX-License-Identifier: AGPL-3.0-or-later
//
//  P4 layout redesign — 1024×600 Dashboard pattern
//  Derived from wardrive.html v2 design language.
//
//  LAYOUT (all dimensions for 1024×600):
//
//  ┌─────────────────────────────────────────────────────────┐
//  │ STATUS BAR 32px (persistent, pm_ui_status_bar)          │
//  ├─────────────────────────────────────────────────────────┤
//  │ TITLEBAR 44px: ← WARDRIVE │ chips │ session │ rec●     │
//  ├───────────────────────────────────────────────────────-─┤
//  │ STATS ROW 100px: WIFI │ BLE │ PROBES │ PACKETS         │
//  ├──────────────────┬──────────────────┬───────────────────┤
//  │ NET LIST 280px   │ CENTER 404px     │ LIVE LOG 340px    │
//  │ (scrollable)     │ (visualization)  │ (feed)            │
//  │                  │                  │                   │
//  │ ~344px tall      │                  │                   │
//  ├──────────────────┴──────────────────┴───────────────────┤
//  │ ACTION BAR 52px: [▶ START] [■ STOP] [⬆ EXPORT] [⚙]    │
//  └─────────────────────────────────────────────────────────┘
//
//  Content area height: 600 - 32(status) - 44(title) = 524px
//  Minus stats 100px, minus action bar 52px = 372px for panels
// ============================================================

// NOTE: This is the redesigned _build_screen() function and
// supporting static functions. Replace the existing versions
// in your pm_app_wardrive.c, keeping all logic functions
// (_ensure_db, _tick, _enter etc.) exactly as they are.
// Only _build_screen() and the label/widget pointers change.

// ── Widget handles (replace existing static declarations) ────

static lv_obj_t* s_wd_screen       = NULL;
static lv_obj_t* s_lbl_session     = NULL;
static lv_obj_t* s_lbl_wifi        = NULL;
static lv_obj_t* s_lbl_ble         = NULL;
static lv_obj_t* s_lbl_probe       = NULL;
static lv_obj_t* s_lbl_pkt         = NULL;
static lv_obj_t* s_net_list        = NULL;  // NEW: scrollable network list
static lv_obj_t* s_live_log        = NULL;  // NEW: right log panel
static lv_obj_t* s_center_canvas   = NULL;  // NEW: center visualization
static lv_obj_t* s_chip_gps        = NULL;  // NEW: GPS status chip
static lv_obj_t* s_chip_db         = NULL;  // NEW: DB/CSV status chip
static lv_obj_t* s_rec_dot         = NULL;  // NEW: recording indicator
static lv_obj_t* s_btn_start       = NULL;  // NEW: start button
static lv_obj_t* s_btn_stop        = NULL;  // NEW: stop button
static bool      s_running         = false;

// ── Colors ───────────────────────────────────────────────────
#define WD_C_WIFI       lv_color_hex(0x00d4ff)   // cyan
#define WD_C_BLE        lv_color_hex(0x00ff88)   // green
#define WD_C_PROBE      lv_color_hex(0xf4a820)   // gold
#define WD_C_PKT        lv_color_hex(0xff3366)   // red
#define WD_C_BG         lv_color_hex(0x050a0e)
#define WD_C_BG2        lv_color_hex(0x080f14)
#define WD_C_BG3        lv_color_hex(0x0c1620)
#define WD_C_BORDER     lv_color_hex(0x1a3a50)
#define WD_C_FG         lv_color_hex(0x7ab8d4)
#define WD_C_FG_BRIGHT  lv_color_hex(0xc8e8f5)
#define WD_C_FG_DIM     lv_color_hex(0x2a5870)

// ── Helper: make a dividing panel ────────────────────────────
static lv_obj_t* _make_panel(lv_obj_t* parent) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_style_bg_color(p, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_side(p, LV_BORDER_SIDE_RIGHT, 0);
    return p;
}

// ── Helper: stat block ────────────────────────────────────────
// Returns the label widget so caller can keep a pointer to update it
static lv_obj_t* _make_stat_block(lv_obj_t* parent,
                                   const char* label_text,
                                   lv_color_t  accent) {
    lv_obj_t* blk = lv_obj_create(parent);
    lv_obj_remove_style_all(blk);
    lv_obj_set_style_bg_color(blk, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(blk, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(blk, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(blk, 1, 0);
    lv_obj_set_style_border_side(blk, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_flex_grow(blk, 1);
    lv_obj_set_height(blk, 100);
    lv_obj_set_layout(blk, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(blk, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(blk, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(blk, 8, 0);

    // Large number label
    lv_obj_t* num = lv_label_create(blk);
    lv_label_set_text(num, "0");
    lv_obj_set_style_text_font(num, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(num, accent, 0);

    // Small category label
    lv_obj_t* lbl = lv_label_create(blk);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, WD_C_FG_DIM, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);

    return num;  // caller stores this to update the number
}

// ── Helper: inline chip ───────────────────────────────────────
static lv_obj_t* _make_chip(lv_obj_t* parent, const char* text,
                              lv_color_t color) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_style_bg_color(chip, color, 0);
    lv_obj_set_style_bg_opa(chip, 30, 0);   // ~12% opacity
    lv_obj_set_style_border_color(chip, color, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_set_style_pad_hor(chip, 8, 0);
    lv_obj_set_style_pad_ver(chip, 3, 0);
    lv_obj_set_height(chip, LV_SIZE_CONTENT);
    lv_obj_set_width(chip, LV_SIZE_CONTENT);

    lv_obj_t* lbl = lv_label_create(chip);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_letter_space(lbl, 1, 0);

    return chip;
}

// ── Helper: action button ─────────────────────────────────────
static lv_obj_t* _make_action_btn(lv_obj_t* parent, const char* label,
                                   lv_color_t color, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, 25, 0);
    lv_obj_set_style_border_color(btn, color, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_pad_hor(btn, 20, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_set_height(btn, 36);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    // Pressed state
    lv_obj_set_style_bg_opa(btn, 80, LV_STATE_PRESSED);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_center(lbl);

    return btn;
}

// ── Log panel: append a line ──────────────────────────────────
// Call this from your WiFi/BLE/probe callbacks
void pm_app_wardrive_log(const char* timestamp, const char* type,
                          const char* content, lv_color_t color) {
    if (!s_live_log) return;

    lv_obj_t* row = lv_obj_create(s_live_log);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(row, 3, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_border_color(row,
        lv_color_hex(0x1a3a50), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);

    // Timestamp
    lv_obj_t* ts = lv_label_create(row);
    lv_label_set_text(ts, timestamp);
    lv_obj_set_style_text_font(ts, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ts, lv_color_hex(0x2a5870), 0);
    lv_obj_set_width(ts, 56);
    lv_obj_set_style_pad_right(ts, 6, 0);

    // Type badge
    lv_obj_t* badge = lv_label_create(row);
    lv_label_set_text(badge, type);
    lv_obj_set_style_text_font(badge, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(badge, color, 0);
    lv_obj_set_width(badge, 48);
    lv_obj_set_style_pad_right(badge, 6, 0);

    // Content
    lv_obj_t* cnt = lv_label_create(row);
    lv_label_set_text(cnt, content);
    lv_obj_set_style_text_font(cnt, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(cnt, lv_color_hex(0xc8e8f5), 0);
    lv_obj_set_flex_grow(cnt, 1);
    lv_label_set_long_mode(cnt, LV_LABEL_LONG_CLIP);

    // Auto-scroll to bottom
    lv_obj_scroll_to_y(s_live_log, LV_COORD_MAX, LV_ANIM_OFF);
}

// ── Network list: add/update an entry ────────────────────────
void pm_app_wardrive_add_network(const char* ssid, const char* bssid,
                                  int rssi, int channel,
                                  const char* enc) {
    if (!s_net_list) return;

    lv_obj_t* item = lv_obj_create(s_net_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_layout(item, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(item, 8, 0);
    lv_obj_set_style_pad_left(item, 12, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(0x1a3a50), 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);

    // SSID line (bright)
    lv_obj_t* ssid_lbl = lv_label_create(item);
    lv_label_set_text(ssid_lbl, ssid[0] ? ssid : "(hidden)");
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ssid_lbl,
        lv_color_hex(0xc8e8f5), 0);
    lv_label_set_long_mode(ssid_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(ssid_lbl, LV_PCT(100));

    // Meta line: BSSID / CH / RSSI / ENC
    char meta[80];
    snprintf(meta, sizeof(meta), "%s  CH%d  %ddBm  %s",
             bssid, channel, rssi, enc ? enc : "OPEN");
    lv_obj_t* meta_lbl = lv_label_create(item);
    lv_label_set_text(meta_lbl, meta);
    lv_obj_set_style_text_font(meta_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(meta_lbl,
        lv_color_hex(0x2a5870), 0);
    lv_label_set_long_mode(meta_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(meta_lbl, LV_PCT(100));
}

// ── Button callbacks ──────────────────────────────────────────
static void _start_cb(lv_event_t* e) {
    (void)e;
    if (s_running) return;
    s_running = true;
    pm_log_i("WARDRIVE", "scan start");
    // TODO: trigger C6 WiFi scan via esp_wifi_scan_start()
    if (s_rec_dot) {
        lv_obj_set_style_bg_color(s_rec_dot,
            lv_color_hex(0xff3366), 0);   // red when recording
    }
    if (s_btn_start)
        lv_obj_set_style_bg_opa(s_btn_start, 80, 0);
}

static void _stop_cb(lv_event_t* e) {
    (void)e;
    if (!s_running) return;
    s_running = false;
    pm_log_i("WARDRIVE", "scan stop");
    if (s_rec_dot) {
        lv_obj_set_style_bg_color(s_rec_dot,
            lv_color_hex(0x2a5870), 0);   // dim when stopped
    }
    if (s_btn_start)
        lv_obj_set_style_bg_opa(s_btn_start, 25, 0);
}

static void _export_cb(lv_event_t* e) {
    (void)e;
    pm_app_wardrive_export_csv();
}

// ── _build_screen (FULL REDESIGN for 1024×600) ───────────────
static void _build_screen(void) {
    // Root screen
    s_wd_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wd_screen, WD_C_BG, 0);
    lv_obj_set_style_bg_opa(s_wd_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_wd_screen, 0, 0);
    lv_obj_set_layout(s_wd_screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_wd_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(s_wd_screen, LV_PCT(100), LV_PCT(100));

    // ── 1. TITLEBAR ──────────────────────────────────────────
    lv_obj_t* titlebar = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_width(titlebar, LV_PCT(100));
    lv_obj_set_height(titlebar, 44);
    lv_obj_set_style_bg_color(titlebar, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(titlebar, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(titlebar, 1, 0);
    lv_obj_set_style_border_side(titlebar,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(titlebar, 14, 0);
    lv_obj_set_style_pad_ver(titlebar, 0, 0);
    lv_obj_set_layout(titlebar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(titlebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(titlebar, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(titlebar, 10, 0);

    // Back button
    lv_obj_t* back = lv_btn_create(titlebar);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, 36, 36);
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_lbl,
        lv_color_hex(0x2a5870), 0);
    lv_obj_set_style_text_color(back_lbl,
        lv_color_hex(0x7ab8d4), LV_STATE_PRESSED);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back,
        [](lv_event_t* e){ pm_launcher_show(); },
        LV_EVENT_CLICKED, NULL);

    // Title
    lv_obj_t* title = lv_label_create(titlebar);
    lv_label_set_text(title, "WARDRIVE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title,
        lv_color_hex(0xc8e8f5), 0);
    lv_obj_set_style_text_letter_space(title, 3, 0);

    // Spacer
    lv_obj_t* sp1 = lv_obj_create(titlebar);
    lv_obj_remove_style_all(sp1);
    lv_obj_set_height(sp1, 1);
    lv_obj_set_flex_grow(sp1, 1);

    // Session label (truncated)
    s_lbl_session = lv_label_create(titlebar);
    lv_label_set_text(s_lbl_session, "session_pending");
    lv_obj_set_style_text_font(s_lbl_session,
        &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_lbl_session,
        lv_color_hex(0x4a7a92), 0);
    lv_obj_set_width(s_lbl_session, 220);
    lv_label_set_long_mode(s_lbl_session, LV_LABEL_LONG_CLIP);

    // C6 Ghost chip
    _make_chip(titlebar, "C6 GHOST",
               lv_color_hex(0x00ff88));

    // GPS chip (will update when GPS locks)
    s_chip_gps = _make_chip(titlebar, "GPS?",
                             lv_color_hex(0xffcc00));

    // DB chip (SQLite or CSV)
    s_chip_db = _make_chip(titlebar, "SQLITE",
                            lv_color_hex(0x00d4ff));

    // Recording dot
    s_rec_dot = lv_obj_create(titlebar);
    lv_obj_remove_style_all(s_rec_dot);
    lv_obj_set_size(s_rec_dot, 10, 10);
    lv_obj_set_style_radius(s_rec_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_rec_dot,
        lv_color_hex(0x2a5870), 0);   // dim = not recording
    lv_obj_set_style_bg_opa(s_rec_dot, LV_OPA_COVER, 0);

    // ── 2. STATS ROW ─────────────────────────────────────────
    lv_obj_t* stats_row = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(stats_row);
    lv_obj_set_width(stats_row, LV_PCT(100));
    lv_obj_set_height(stats_row, 100);
    lv_obj_set_style_bg_color(stats_row, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(stats_row, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(stats_row, 1, 0);
    lv_obj_set_style_border_side(stats_row,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_layout(stats_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_START,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_lbl_wifi  = _make_stat_block(stats_row, "WIFI",   WD_C_WIFI);
    s_lbl_ble   = _make_stat_block(stats_row, "BLE",    WD_C_BLE);
    s_lbl_probe = _make_stat_block(stats_row, "PROBES", WD_C_PROBE);
    s_lbl_pkt   = _make_stat_block(stats_row, "PACKETS",WD_C_PKT);

    // ── 3. MAIN CONTENT AREA (3 columns) ─────────────────────
    // Height: 600 - 44(title) - 100(stats) - 52(action) = 404px
    lv_obj_t* content = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_layout(content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(content, 0, 0);

    // 3a. LEFT PANEL — network list (280px)
    lv_obj_t* left = lv_obj_create(content);
    lv_obj_remove_style_all(left);
    lv_obj_set_width(left, 280);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_bg_color(left, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(left, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);

    // Left header
    lv_obj_t* left_hdr = lv_obj_create(left);
    lv_obj_remove_style_all(left_hdr);
    lv_obj_set_width(left_hdr, LV_PCT(100));
    lv_obj_set_height(left_hdr, 28);
    lv_obj_set_style_bg_color(left_hdr, WD_C_BG3, 0);
    lv_obj_set_style_bg_opa(left_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(left_hdr, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(left_hdr, 1, 0);
    lv_obj_set_style_border_side(left_hdr,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(left_hdr, 10, 0);
    lv_obj_t* left_hdr_lbl = lv_label_create(left_hdr);
    lv_label_set_text(left_hdr_lbl, "NETWORKS");
    lv_obj_set_style_text_font(left_hdr_lbl,
        &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(left_hdr_lbl,
        lv_color_hex(0x2a5870), 0);
    lv_obj_set_style_text_letter_space(left_hdr_lbl, 2, 0);
    lv_obj_align(left_hdr_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Network list (scrollable)
    s_net_list = lv_obj_create(left);
    lv_obj_remove_style_all(s_net_list);
    lv_obj_set_width(s_net_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_net_list, 1);
    lv_obj_set_layout(s_net_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_net_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_net_list, LV_DIR_VER);
    lv_obj_set_style_pad_all(s_net_list, 0, 0);

    // 3b. CENTER PANEL — visualization (fills remaining width)
    lv_obj_t* center = lv_obj_create(content);
    lv_obj_remove_style_all(center);
    lv_obj_set_flex_grow(center, 1);
    lv_obj_set_height(center, LV_PCT(100));
    lv_obj_set_style_bg_color(center, WD_C_BG, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);

    // Center placeholder: "GPS LOCK FOR MAP VIEW"
    // When GPS locks, replace with actual map/chart
    lv_obj_t* center_placeholder = lv_label_create(center);
    lv_label_set_text(center_placeholder,
        "MAP / VISUALIZATION\n\nGPS lock required for map view.\n"
        "When GPS locked, network density\n"
        "map will render here.\n\n"
        "Live channel activity chart\nrendering in next build.");
    lv_obj_set_style_text_font(center_placeholder,
        &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(center_placeholder,
        lv_color_hex(0x1e4a62), 0);
    lv_obj_set_style_text_align(center_placeholder,
        LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(center_placeholder);
    s_center_canvas = center_placeholder;

    // 3c. RIGHT PANEL — live log (340px)
    lv_obj_t* right = lv_obj_create(content);
    lv_obj_remove_style_all(right);
    lv_obj_set_width(right, 340);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_style_bg_color(right, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(right, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_border_side(right, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);

    // Right header
    lv_obj_t* right_hdr = lv_obj_create(right);
    lv_obj_remove_style_all(right_hdr);
    lv_obj_set_width(right_hdr, LV_PCT(100));
    lv_obj_set_height(right_hdr, 28);
    lv_obj_set_style_bg_color(right_hdr, WD_C_BG3, 0);
    lv_obj_set_style_bg_opa(right_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(right_hdr, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(right_hdr, 1, 0);
    lv_obj_set_style_border_side(right_hdr,
        LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_hor(right_hdr, 10, 0);
    lv_obj_t* right_hdr_lbl = lv_label_create(right_hdr);
    lv_label_set_text(right_hdr_lbl, "LIVE CAPTURE");
    lv_obj_set_style_text_font(right_hdr_lbl,
        &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(right_hdr_lbl,
        lv_color_hex(0x2a5870), 0);
    lv_obj_set_style_text_letter_space(right_hdr_lbl, 2, 0);
    lv_obj_align(right_hdr_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Live log (scrollable)
    s_live_log = lv_obj_create(right);
    lv_obj_remove_style_all(s_live_log);
    lv_obj_set_width(s_live_log, LV_PCT(100));
    lv_obj_set_flex_grow(s_live_log, 1);
    lv_obj_set_layout(s_live_log, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_live_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_live_log, LV_DIR_VER);
    lv_obj_set_style_pad_all(s_live_log, 0, 0);

    // Initial log entry
    pm_app_wardrive_log("READY", "SYS",
        "Session initialized", lv_color_hex(0x4a7a92));

    // ── 4. ACTION BAR ─────────────────────────────────────────
    lv_obj_t* action_bar = lv_obj_create(s_wd_screen);
    lv_obj_remove_style_all(action_bar);
    lv_obj_set_width(action_bar, LV_PCT(100));
    lv_obj_set_height(action_bar, 52);
    lv_obj_set_style_bg_color(action_bar, WD_C_BG2, 0);
    lv_obj_set_style_bg_opa(action_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(action_bar, WD_C_BORDER, 0);
    lv_obj_set_style_border_width(action_bar, 1, 0);
    lv_obj_set_style_border_side(action_bar,
        LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_layout(action_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_bar, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(action_bar, 16, 0);
    lv_obj_set_style_pad_column(action_bar, 12, 0);

    s_btn_start = _make_action_btn(action_bar, LV_SYMBOL_PLAY " START",
                                    lv_color_hex(0x00ff88), _start_cb);
    s_btn_stop  = _make_action_btn(action_bar, LV_SYMBOL_STOP " STOP",
                                    lv_color_hex(0xff3366), _stop_cb);
    _make_action_btn(action_bar, LV_SYMBOL_UPLOAD " EXPORT",
                     lv_color_hex(0x00d4ff), _export_cb);
    _make_action_btn(action_bar, LV_SYMBOL_GPS " MAP",
                     lv_color_hex(0xf4a820),
                     [](lv_event_t* e){ pm_log_i("WD", "map todo"); });
    _make_action_btn(action_bar, LV_SYMBOL_SETTINGS,
                     lv_color_hex(0x4a7a92),
                     [](lv_event_t* e){ pm_log_i("WD", "settings todo"); });
}

// ── _render: update labels ────────────────────────────────────
// (Keep existing _render function, it already does null checks)
// The label pointers now point to the new stat block numbers
// so updates work without change.

// ── _enter: lazy-build pattern (KEEP AS IS from earlier fix) ──
// static void _enter(void) {
//     if (!s_wd_screen) { _build_screen(); }
//     if (s_wd_screen) lv_screen_load(s_wd_screen);
//     pm_log_i(TAG, "enter");
//     _ensure_db();
//     _render();
// }
