// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_voice_terminal.c — Voice → STT → Gemini → TTS loop
//
//  State machine:
//    IDLE → RECORDING → STT_PENDING → GEMINI_PENDING →
//    TTS_PENDING → PLAYING → IDLE
//
//  Each transition is logged to scrollback. Network legs go
//  through C6 http_post/http_response (still pending).
// ============================================================

#include "pm_app_voice_terminal.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"
#include "pm_audio.h"
#include "pm_c6_bridge.h"
#include "pm_nosql.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_VT";

#define SCROLLBACK_SZ  (32 * 1024)
#define REC_PATH       "/sd/voice/last_in.wav"
#define TTS_PATH       "/sd/voice/last_out.mp3"

typedef enum {
    VT_IDLE,
    VT_RECORDING,
    VT_STT_PENDING,
    VT_GEMINI_PENDING,
    VT_TTS_PENDING,
    VT_PLAYING,
} vt_state_t;

static vt_state_t s_state = VT_IDLE;
static char*      s_scrollback = NULL;
static int        s_scrollback_len = 0;
static bool       s_tts_enabled = true;
static bool       s_kbd_mode    = false;
static char       s_input[512];
static int        s_input_len = 0;

// LVGL
static void* s_screen = NULL;

// ─────────────────────────────────────────────
//  Scrollback
// ─────────────────────────────────────────────
static void _sb_print(const char* tag, const char* msg) {
    if (!s_scrollback) return;
    char line[640];
    int n = snprintf(line, sizeof(line), "[%s] %s\n", tag, msg);
    if (n <= 0) return;
    if (s_scrollback_len + n + 1 >= SCROLLBACK_SZ) {
        int half = SCROLLBACK_SZ / 2;
        memmove(s_scrollback, s_scrollback + half, s_scrollback_len - half);
        s_scrollback_len -= half;
    }
    memcpy(s_scrollback + s_scrollback_len, line, n);
    s_scrollback_len += n;
    s_scrollback[s_scrollback_len] = 0;
    // TODO_LVGL: lv_label_set_text(...)
}

// ─────────────────────────────────────────────
//  Toggles
// ─────────────────────────────────────────────
void pm_app_voice_terminal_toggle_tts(void) {
    s_tts_enabled = !s_tts_enabled;
    _sb_print("INFO", s_tts_enabled ? "TTS ON" : "TTS OFF");
}

void pm_app_voice_terminal_toggle_keyboard_mode(void) {
    s_kbd_mode = !s_kbd_mode;
    _sb_print("INFO", s_kbd_mode ? "Keyboard mode" : "Voice mode");
}

// ─────────────────────────────────────────────
//  Record
// ─────────────────────────────────────────────
void pm_app_voice_terminal_record_press(void) {
    if (s_state != VT_IDLE) return;
    if (s_kbd_mode) return;
    pm_file_mkdir("/sd/voice");
    if (!pm_audio_record_start(REC_PATH)) {
        _sb_print("ERROR", "mic init failed");
        return;
    }
    s_state = VT_RECORDING;
    _sb_print("REC", "recording…");
}

void pm_app_voice_terminal_record_release(void) {
    if (s_state != VT_RECORDING) return;
    pm_audio_record_stop();
    s_state = VT_STT_PENDING;
    _sb_print("STT", "uploading audio… (C6 http_post pending)");

    // TODO when C6 http_post is online:
    //   1. read REC_PATH into PSRAM buffer
    //   2. base64-encode
    //   3. POST to speech.googleapis.com /v1/speech:recognize
    //      ?key=GOOGLE_CLOUD_API_KEY
    //   4. parse transcript → call _gemini_send(transcript)
    s_state = VT_IDLE;
}

// ─────────────────────────────────────────────
//  Keyboard mode input
// ─────────────────────────────────────────────
void pm_app_voice_terminal_input_char(char c) {
    if (!s_kbd_mode) return;
    if (c == '\n' || c == '\r') {
        s_input[s_input_len] = 0;
        if (s_input_len > 0) {
            char tmp[128];
            snprintf(tmp, sizeof(tmp), "user: %s", s_input);
            _sb_print("USER", tmp);
            // TODO: _gemini_send(s_input);
            _sb_print("GEMINI", "(transport pending)");
        }
        s_input_len = 0;
        s_input[0]  = 0;
    } else if (c == 8 || c == 127) {
        if (s_input_len > 0) s_input_len--;
    } else if (s_input_len < (int)sizeof(s_input) - 1) {
        s_input[s_input_len++] = c;
    }
    s_input[s_input_len] = 0;
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: scrollback area, status banner (state-colored),
    //            big PTT button, [TTS] [KBD] toggles, input line
    //            (in kbd mode), [QUIT].
    pm_log_d(TAG, "state=%d sb=%d", (int)s_state, s_scrollback_len);
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("VOICE",
        "VOICE app — UI ready");
}

static void _init(void) {
    pm_audio_init();
    if (!s_scrollback) {
        s_scrollback = (char*)pm_psram_alloc(SCROLLBACK_SZ);
        if (s_scrollback) { s_scrollback[0] = 0; s_scrollback_len = 0; }
    }
    _build_screen();
}

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _sb_print("READY", s_kbd_mode ? "type and press ENTER"
                                   : "hold SPACE to record");
    _render();
}

static void _tick(uint32_t elapsed_ms) {
    (void)elapsed_ms;
    if (s_state == VT_RECORDING) pm_audio_record_drive();
    if (s_state == VT_PLAYING)   pm_audio_play_drive();
    _render();
}

static void _exit_(void) {
    pm_log_i(TAG, "exit");
    if (pm_audio_record_is_recording()) pm_audio_record_stop();
    if (pm_audio_play_is_playing())     pm_audio_play_stop();
}

static void _deinit(void) {
    if (s_scrollback) { pm_psram_free(s_scrollback); s_scrollback = NULL; }
}

static const pm_app_t _APP = {
    .id           = "voice_terminal",
    .display_name = "VOICE",
    .category     = PM_CAT_COMMS,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = _tick,
    .exit         = _exit_,
    .deinit       = _deinit,
};

const pm_app_t* pm_app_voice_terminal(void) { return &_APP; }
