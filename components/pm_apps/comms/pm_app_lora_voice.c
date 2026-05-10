// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_lora_voice.c — PTT voice over LoRa (scaffold)
//
//  The state machine is real. Three components are TODO:
//    - SX1262 driver bring-up (RadioLib has a port; or
//      esp_lora_sx1262 from managed components).
//    - Codec2 encoder/decoder (open source — github
//      meshtastic/ESP32_Codec2 is C and fits IDF cleanly).
//    - I2S in/out wired through pm_audio (full-duplex
//      not yet supported in pm_audio — single direction).
//
//  Until those land, the app is a UI scaffold + state
//  machine that simulates TX/RX cycles for testing.
// ============================================================

#include "pm_app_lora_voice.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_audio.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_LORA_VOICE";

// Voice config (Codec2 3200 bps mode)
#define VOICE_SAMPLE_RATE  8000
#define VOICE_CODEC_FRAME_BYTES  44
#define VOICE_CODEC_PCM_PER_FRAME 320     // 40ms @ 8kHz

typedef enum { ST_IDLE, ST_TX, ST_RX } state_t;

static state_t   s_state    = ST_IDLE;
static bool      s_active   = false;     // SPI Treaty consult
static int       s_volume   = 14;
static char      s_status[80] = "Idle  —  press PTT to transmit";
static uint32_t  s_state_started_ms = 0;
static int       s_tx_frames = 0;
static int       s_rx_frames = 0;
static int       s_rx_peak   = 0;        // last RX RSSI

// ─────────────────────────────────────────────
//  Public flag for treaty discipline
// ─────────────────────────────────────────────
bool pm_app_lora_voice_active(void) { return s_active; }

// ─────────────────────────────────────────────
//  Radio bring-up (Phase 12 — pm_lora wired, Phase 13 — radio-aware)
// ─────────────────────────────────────────────
#include "pm_lora.h"
#include "pm_radio.h"

static bool _radio_init(void) {
    if (pm_radio_kind() != PM_RADIO_SX1262) {
        pm_log_w(TAG, "voice requires SX1262; current radio: %s",
                 pm_radio_name(pm_radio_kind()));
        return false;
    }
    if (!pm_lora_is_initialized() && pm_lora_init() != PM_LORA_OK) {
        pm_log_w(TAG, "pm_lora_init failed");
        return false;
    }
    if (!pm_lora_take(500, "lora_voice")) {
        pm_log_w(TAG, "radio busy (held by other app)");
        return false;
    }
    if (pm_lora_set_mode_voice() != PM_LORA_OK) {
        pm_lora_give();
        return false;
    }
    pm_log_i(TAG, "voice mode active");
    return true;
}

static void _radio_deinit(void) {
    pm_lora_give();
}

// ─────────────────────────────────────────────
//  PTT
// ─────────────────────────────────────────────
void pm_app_lora_voice_ptt_press(void) {
    if (s_state == ST_TX) return;
    s_state    = ST_TX;
    s_active   = true;
    s_tx_frames = 0;
    s_state_started_ms = pm_millis();
    snprintf(s_status, sizeof(s_status), "● TRANSMIT");
    pm_log_i(TAG, "PTT down — TX start");
    // TODO: pm_audio_record_start_stream("voice_tx") — not the WAV recorder
    //       but a frame-callback variant that hands 320-sample blocks to
    //       Codec2, which yields 44-byte frames written to SX1262.
}

void pm_app_lora_voice_ptt_release(void) {
    if (s_state != ST_TX) return;
    pm_log_i(TAG, "PTT up — TX %d frames", s_tx_frames);
    // TODO: pm_audio_record_stop_stream(); send EOT marker.
    s_state  = ST_IDLE;
    s_active = false;
    snprintf(s_status, sizeof(s_status), "Idle  —  press PTT to transmit");
}

// ─────────────────────────────────────────────
//  RX (poll model — driven by tick)
// ─────────────────────────────────────────────
static void _try_rx(void) {
    // TODO: poll SX1262 for frame, decode codec2 → pm_audio_play_frame().
    // For scaffold, simulate occasional RX every ~2s when idle.
    static uint32_t last_sim = 0;
    uint32_t now = pm_millis();
    if (now - last_sim < 2000) return;
    last_sim = now;
    if (s_state != ST_IDLE) return;
    // No simulated event by default — keep clean. Toggle a debug
    // build flag if useful.
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: status banner (color by state),
    //            peak meter (TX uses pm_audio_record_peak,
    //            RX uses s_rx_peak), [PTT] big button,
    //            stats footer (TX frames / RX frames / last RSSI).
    pm_log_d(TAG, "%s [tx=%d rx=%d]", s_status, s_tx_frames, s_rx_frames);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("LORA PTT",
        "LORA PTT app — UI ready");
}

static void _init(void) {
    pm_audio_init();
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    pm_audio_set_volume(s_volume);
    if (!_radio_init()) {
        snprintf(s_status, sizeof(s_status), "Radio init failed");
    }
    s_state  = ST_IDLE;
    s_active = false;
    _render();
}

static uint32_t s_last_render_ms = 0;
static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    if (s_state == ST_TX) {
        // TODO: drive encoder → radio. Frame callback updates s_tx_frames.
    }
    _try_rx();
    uint32_t now = pm_millis();
    if (now - s_last_render_ms >= 100) {
        s_last_render_ms = now;
        _render();
    }
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    pm_app_lora_voice_ptt_release();
    _radio_deinit();
}

static const pm_app_t _APP = {
    .id           = "lora_voice",
    .display_name = "LORA PTT",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_lora_voice(void) { return &_APP; }
