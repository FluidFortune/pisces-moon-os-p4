// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_apps_register.c — Central app registration
//
//  Phase 8 — ALL CATEGORIES ONLINE.
//  SYSTEM (8) + TOOLS (5) + INTEL (7) + GAMES (7) +
//  MEDIA (2) + COMMS (6) + CYBER (14) = 49 apps.
// ============================================================

#include "pm_app.h"
#include "pm_hal.h"

// SYSTEM
#include "pm_app_about.h"
#include "pm_app_system.h"
#include "pm_app_files.h"
#include "pm_app_filemgr.h"
#include "pm_app_elf_browser.h"
#include "pm_app_gamepad.h"
#include "pm_app_bridge.h"
#include "pm_app_micropython.h"
#include "pm_app_c6_flasher.h"

// TOOLS
#include "pm_app_notepad.h"
#include "pm_app_calculator.h"
#include "pm_app_clock.h"
#include "pm_app_calendar.h"
#include "pm_app_etch.h"

// INTEL
#include "pm_app_terminal.h"
#include "pm_app_gemini_log.h"
#include "pm_app_ref_med.h"
#include "pm_app_ref_surv.h"
#include "pm_app_baseball.h"
#include "pm_app_trails.h"
#include "pm_app_ssh.h"

// GAMES
#include "pm_app_snake.h"
#include "pm_app_pacman.h"
#include "pm_app_galaga.h"
#include "pm_app_chess.h"
#include "pm_app_doom.h"
#include "pm_app_simcity.h"
#include "pm_app_retro_elf.h"

// MEDIA
#include "pm_app_audio_player.h"
#include "pm_app_audio_recorder.h"

// COMMS
#include "pm_app_gps.h"
#include "pm_app_wifi.h"
#include "pm_app_bluetooth.h"
#include "pm_app_lora_voice.h"
#include "pm_app_mesh_messenger.h"
#include "pm_app_voice_terminal.h"

// CYBER
#include "pm_app_wardrive.h"
#include "pm_app_bt_radar.h"
#include "pm_app_pkt_sniffer.h"
#include "pm_app_beacon.h"
#include "pm_app_net_scanner.h"
#include "pm_app_hash_tool.h"
#include "pm_app_ble_gatt.h"
#include "pm_app_wpa_hs.h"
#include "pm_app_rf_spectrum.h"
#include "pm_app_probe_intel.h"
#include "pm_app_pkt_analysis.h"
#include "pm_app_ble_ducky.h"
#include "pm_app_usb_ducky.h"
#include "pm_app_wifi_ducky.h"

#define REGISTER_IF(getter) \
    do { const pm_app_t* a = (getter); if (a) pm_app_register(a); } while (0)

void main_register_apps(void) {
    pm_log_i("PM_APPS", "registering apps...");

    // SYSTEM (9)
    REGISTER_IF(pm_app_files());
    REGISTER_IF(pm_app_filemgr());
    REGISTER_IF(pm_app_about());
    REGISTER_IF(pm_app_system());
    REGISTER_IF(pm_app_micropython());
    REGISTER_IF(pm_app_elf_browser());
    REGISTER_IF(pm_app_gamepad());
    REGISTER_IF(pm_app_bridge());
    REGISTER_IF(pm_app_c6_flasher());

    // TOOLS (5)
    REGISTER_IF(pm_app_notepad());
    REGISTER_IF(pm_app_calculator());
    REGISTER_IF(pm_app_clock());
    REGISTER_IF(pm_app_calendar());
    REGISTER_IF(pm_app_etch());

    // INTEL (7)
    REGISTER_IF(pm_app_terminal());
    REGISTER_IF(pm_app_gemini_log());
    REGISTER_IF(pm_app_ref_med());
    REGISTER_IF(pm_app_ref_surv());
    REGISTER_IF(pm_app_baseball());
    REGISTER_IF(pm_app_trails());
    REGISTER_IF(pm_app_ssh());

    // GAMES (7)
    REGISTER_IF(pm_app_snake());
    REGISTER_IF(pm_app_pacman());
    REGISTER_IF(pm_app_galaga());
    REGISTER_IF(pm_app_chess());
    REGISTER_IF(pm_app_doom());
    REGISTER_IF(pm_app_simcity());
    REGISTER_IF(pm_app_retro_elf());

    // MEDIA (2)
    REGISTER_IF(pm_app_audio_player());
    REGISTER_IF(pm_app_audio_recorder());

    // COMMS (6)
    REGISTER_IF(pm_app_gps());
    REGISTER_IF(pm_app_wifi());
    REGISTER_IF(pm_app_bluetooth());
    REGISTER_IF(pm_app_lora_voice());
    REGISTER_IF(pm_app_mesh_messenger());
    REGISTER_IF(pm_app_voice_terminal());

    // CYBER (14)
    REGISTER_IF(pm_app_wardrive());
    REGISTER_IF(pm_app_bt_radar());
    REGISTER_IF(pm_app_pkt_sniffer());
    REGISTER_IF(pm_app_beacon());
    REGISTER_IF(pm_app_net_scanner());
    REGISTER_IF(pm_app_hash_tool());
    REGISTER_IF(pm_app_ble_gatt());
    REGISTER_IF(pm_app_wpa_hs());
    REGISTER_IF(pm_app_rf_spectrum());
    REGISTER_IF(pm_app_probe_intel());
    REGISTER_IF(pm_app_pkt_analysis());
    REGISTER_IF(pm_app_ble_ducky());
    REGISTER_IF(pm_app_usb_ducky());
    REGISTER_IF(pm_app_wifi_ducky());

    pm_log_i("PM_APPS", "all 49 apps registered");
}
