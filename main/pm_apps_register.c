// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_apps_register.c — Central app registration
//
//  Phase 17 — S3 parity pass.
//  SYSTEM (9) + TOOLS (9) + INTEL (8) + GAMES (19) +
//  MEDIA (3) + COMMS (7) + CYBER (23) = 78 apps.
//
//  12 of the GAMES entries (tetris, pole_position, mario_bros,
//  breakout, 2048, minesweeper, connect4, simon, solitaire,
//  asteroids, space_invaders, frogger) are launcher stubs —
//  they open to a styled placeholder. Full LVGL ports of those
//  games are tracked separately.
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
#include "pm_app_keytest.h"
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
#include "pm_app_camera.h"           // Phase 15

// COMMS
#include "pm_app_gps.h"
#include "pm_app_wifi.h"
#include "pm_app_bluetooth.h"
#include "pm_app_lora_voice.h"
#include "pm_app_mesh_messenger.h"
#include "pm_app_voice_terminal.h"

// TOOLS — Phase 15 additions
#include "pm_app_camera_qr.h"

// CYBER
#include "pm_app_wardrive.h"
#include "pm_app_silas_creek.h"
#include "pm_app_clinician.h"
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
// CYBER — Phase 15 additions
#include "pm_app_nfc_reader.h"
#include "pm_app_nfc_clone.h"
#include "pm_app_nfc_emulate.h"
#include "pm_app_amiibo.h"
#include "pm_app_secondary_scan.h"
// Phase 17 additions (S3 parity)
#include "pm_app_wardrive_inspect.h"
#include "pm_app_tracker_scan.h"
#include "pm_app_ereader.h"
#include "pm_app_contacts.h"
#include "pm_app_tetris.h"
#include "pm_app_pole_position.h"
#include "pm_app_mario_bros.h"
#include "pm_app_breakout.h"
#include "pm_app_2048.h"
#include "pm_app_minesweeper.h"
#include "pm_app_connect4.h"
#include "pm_app_simon.h"
#include "pm_app_solitaire.h"
#include "pm_app_asteroids.h"
#include "pm_app_space_invaders.h"
#include "pm_app_frogger.h"
#include "pm_app_weather.h"
#include "pm_app_rss.h"

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

    // TOOLS (6 + 1 Phase 15 + 2 Phase 17)
    REGISTER_IF(pm_app_notepad());
    REGISTER_IF(pm_app_keytest());
    REGISTER_IF(pm_app_calculator());
    REGISTER_IF(pm_app_clock());
    REGISTER_IF(pm_app_calendar());
    REGISTER_IF(pm_app_etch());
    REGISTER_IF(pm_app_camera_qr());        // Phase 15
    REGISTER_IF(pm_app_ereader());          // Phase 17 (S3 parity)
    REGISTER_IF(pm_app_contacts());         // Phase 17 (S3 parity)

    // INTEL (7 + 1 Phase 17)
    REGISTER_IF(pm_app_terminal());
    REGISTER_IF(pm_app_gemini_log());
    REGISTER_IF(pm_app_ref_med());
    REGISTER_IF(pm_app_ref_surv());
    REGISTER_IF(pm_app_baseball());
    REGISTER_IF(pm_app_trails());
    REGISTER_IF(pm_app_ssh());
    REGISTER_IF(pm_app_rss());              // Phase 17 (S3 parity)

    // GAMES (7 + 12 Phase 17 stubs)
    REGISTER_IF(pm_app_snake());
    REGISTER_IF(pm_app_pacman());
    REGISTER_IF(pm_app_galaga());
    REGISTER_IF(pm_app_chess());
    REGISTER_IF(pm_app_doom());
    REGISTER_IF(pm_app_simcity());
    REGISTER_IF(pm_app_retro_elf());
    // Phase 17 stubs (S3 parity — full ports pending)
    REGISTER_IF(pm_app_tetris());
    REGISTER_IF(pm_app_pole_position());
    REGISTER_IF(pm_app_mario_bros());
    REGISTER_IF(pm_app_breakout());
    REGISTER_IF(pm_app_2048());
    REGISTER_IF(pm_app_minesweeper());
    REGISTER_IF(pm_app_connect4());
    REGISTER_IF(pm_app_simon());
    REGISTER_IF(pm_app_solitaire());
    REGISTER_IF(pm_app_asteroids());
    REGISTER_IF(pm_app_space_invaders());
    REGISTER_IF(pm_app_frogger());

    // MEDIA (2 + 1 Phase 15)
    REGISTER_IF(pm_app_audio_player());
    REGISTER_IF(pm_app_audio_recorder());
    REGISTER_IF(pm_app_camera());           // Phase 15

    // COMMS (6 + 1 Phase 17)
    REGISTER_IF(pm_app_gps());
    REGISTER_IF(pm_app_wifi());
    REGISTER_IF(pm_app_bluetooth());
    REGISTER_IF(pm_app_lora_voice());
    REGISTER_IF(pm_app_mesh_messenger());
    REGISTER_IF(pm_app_voice_terminal());
    REGISTER_IF(pm_app_weather());          // Phase 17 (S3 parity)

    // CYBER (14 + 5 Phase 15 + 2 Phase 17)
    REGISTER_IF(pm_app_wardrive());
    REGISTER_IF(pm_app_silas_creek());
    REGISTER_IF(pm_app_clinician());
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
    REGISTER_IF(pm_app_nfc_reader());       // Phase 15
    REGISTER_IF(pm_app_nfc_clone());        // Phase 15
    REGISTER_IF(pm_app_nfc_emulate());      // Phase 15
    REGISTER_IF(pm_app_amiibo());           // Phase 15
    REGISTER_IF(pm_app_secondary_scan());   // Phase 15
    REGISTER_IF(pm_app_wardrive_inspect()); // Phase 17 (S3 parity)
    REGISTER_IF(pm_app_tracker_scan());     // Phase 17 (S3 parity)

    pm_log_i("PM_APPS", "app registration complete: %d apps", pm_app_count());
}
