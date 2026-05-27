// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_input.h — Unified input dispatcher
//
//  This board has no physical keyboard and no trackball — only
//  the capacitive touchscreen. Apps that need text input or
//  directional/button input get it from one of three sources:
//
//    1. On-screen QWERTY overlay (pm_ui_keyboard)
//    2. On-screen virtual gamepad overlay (pm_ui_gamepad)
//    3. A paired Bluetooth HID device (8BitDo gamepad,
//       BT keyboard) reported by the C6 over the bridge
//    4. A Cardputer ADV module tethered over the external I2C bus
//
//  All three converge into this dispatcher. Apps subscribe to
//  logical events and don't care which source produced them.
//
//  Use:
//    pm_input_subscribe(my_handler, my_user);
//    void my_handler(const pm_input_event_t* e, void* user) {
//        if (e->kind == PM_INPUT_BUTTON && e->code == PM_BTN_A && e->down) { ... }
//    }
//
//  Touch gestures from the screen are handled by LVGL directly
//  and aren't routed through this layer.
// ============================================================

#ifndef PM_INPUT_H
#define PM_INPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Event kinds ─────────────────────────────────────────────
typedef enum {
    PM_INPUT_KEY     = 0,    // text-style key (ASCII or special)
    PM_INPUT_BUTTON  = 1,    // gamepad button (A/B/X/Y/L/R/Start/Select)
    PM_INPUT_DPAD    = 2,    // gamepad d-pad direction
    PM_INPUT_STICK   = 3,    // analog stick (reserved; 8BitDo Zero has none)
} pm_input_kind_t;

// ── Key codes (used when kind == PM_INPUT_KEY) ──────────────
// For printable characters, code is the ASCII byte. For special
// keys, codes start at 0x100.
#define PM_KEY_BACKSPACE  0x08
#define PM_KEY_TAB        0x09
#define PM_KEY_ENTER      0x0A
#define PM_KEY_ESC        0x1B
#define PM_KEY_SPACE      0x20
// (printable ASCII fills 0x20..0x7E)
#define PM_KEY_DELETE     0x7F

#define PM_KEY_UP         0x101
#define PM_KEY_DOWN       0x102
#define PM_KEY_LEFT       0x103
#define PM_KEY_RIGHT      0x104
#define PM_KEY_HOME       0x105
#define PM_KEY_END        0x106
#define PM_KEY_PAGEUP     0x107
#define PM_KEY_PAGEDOWN   0x108
#define PM_KEY_F1         0x110
#define PM_KEY_F12        0x11B

// ── Button codes (used when kind == PM_INPUT_BUTTON) ────────
typedef enum {
    PM_BTN_A      = 0,
    PM_BTN_B      = 1,
    PM_BTN_X      = 2,
    PM_BTN_Y      = 3,
    PM_BTN_L      = 4,    // L-shoulder
    PM_BTN_R      = 5,    // R-shoulder
    PM_BTN_SELECT = 6,
    PM_BTN_START  = 7,
    PM_BTN_HOME   = 8,    // 8BitDo "heart"/home
} pm_button_t;

// ── D-pad directions (used when kind == PM_INPUT_DPAD) ──────
typedef enum {
    PM_DPAD_NONE  = 0,
    PM_DPAD_UP    = 1,
    PM_DPAD_DOWN  = 2,
    PM_DPAD_LEFT  = 4,
    PM_DPAD_RIGHT = 8,
    // diagonals are bitwise OR (UP|RIGHT, etc.)
} pm_dpad_t;

// ── Event source (for diagnostics) ──────────────────────────
typedef enum {
    PM_INPUT_SRC_VIRTUAL_KEYBOARD = 0,
    PM_INPUT_SRC_VIRTUAL_GAMEPAD  = 1,
    PM_INPUT_SRC_BT_GAMEPAD       = 2,
    PM_INPUT_SRC_BT_KEYBOARD      = 3,
    PM_INPUT_SRC_I2C_KEYBOARD     = 4,
} pm_input_source_t;

// ── Event struct ────────────────────────────────────────────
typedef struct {
    pm_input_kind_t   kind;
    pm_input_source_t source;
    uint32_t          code;     // key/button code OR pm_dpad_t bitmask
    bool              down;     // true=press, false=release
    int16_t           x, y;     // analog axis (reserved)
    uint32_t          timestamp;
} pm_input_event_t;

// ── Subscription ────────────────────────────────────────────
typedef void (*pm_input_handler_t)(const pm_input_event_t* e, void* user);

// Subscribe a handler. Returns a token usable in unsubscribe.
// Up to 8 simultaneous subscribers.
int  pm_input_subscribe(pm_input_handler_t cb, void* user);
void pm_input_unsubscribe(int token);

// Inject an event into the dispatcher (used by virtual widgets
// and by the C6 bridge handler when HID events arrive).
void pm_input_post(const pm_input_event_t* e);

// Diagnostic — most recent event seen, useful for the gamepad
// test screen. Returns false if no events have flowed yet.
bool pm_input_last(pm_input_event_t* out);

// Source state — "is anything currently connected?"
bool pm_input_bt_gamepad_connected(void);
bool pm_input_bt_keyboard_connected(void);

// Set by the C6 bridge handler when device state changes.
void pm_input_set_bt_gamepad_connected(bool yes, const char* name);
void pm_input_set_bt_keyboard_connected(bool yes, const char* name);

const char* pm_input_bt_gamepad_name(void);
const char* pm_input_bt_keyboard_name(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_INPUT_H
