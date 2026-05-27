// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_gamepad.h — 8BitDo Zero 2 (BLE HID) pairing/test
//
//  On P4, BLE lives on the C6. The gamepad pairing flow goes
//  through a C6 command:
//    {"cmd":"hid_pair","device":"8bitdo_zero2"}
//  and the C6 streams gamepad events back as:
//    {"event":"hid_input","buttons":0x0001,"x":0,"y":-1}
//
//  Phase 2 stub: status panel + button-test grid. The hid
//  command path is documented but C6-side implementation
//  is pending.
// ============================================================

#ifndef PM_APP_GAMEPAD_H
#define PM_APP_GAMEPAD_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

// Button bit masks — match 8BitDo Zero 2 HID report layout.
#define PM_GP_UP      0x0001
#define PM_GP_DOWN    0x0002
#define PM_GP_LEFT    0x0004
#define PM_GP_RIGHT   0x0008
#define PM_GP_A       0x0010
#define PM_GP_B       0x0020
#define PM_GP_X       0x0040
#define PM_GP_Y       0x0080
#define PM_GP_L       0x0100
#define PM_GP_R       0x0200
#define PM_GP_START   0x0400
#define PM_GP_SELECT  0x0800
#define PM_GP_HOME    0x1000

typedef struct {
    bool     connected;
    char     device_name[40];
    uint16_t buttons;
    uint32_t last_input_ms;
} pm_gamepad_state_t;

const pm_gamepad_state_t* pm_gamepad_state(void);

// Called from pm_c6_bridge dispatch when an hid_input event arrives.
// (Once C6 firmware emits hid_input events; until then, no-op.)
void pm_gamepad_on_hid_input(uint16_t buttons, int x, int y);
void pm_gamepad_on_hid_connect(const char* device_name);
void pm_gamepad_on_hid_disconnect(void);

const pm_app_t* pm_app_gamepad(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_GAMEPAD_H
