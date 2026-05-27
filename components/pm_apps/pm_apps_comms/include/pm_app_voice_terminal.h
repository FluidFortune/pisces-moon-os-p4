// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_voice_terminal.h — Voice loop with Gemini
//
//  Flow:
//    SPACE press  → record from NS4168 mic via pm_audio_record_*
//    SPACE release → stop record, send WAV to Google Cloud STT
//                     (via C6 http_post) → text
//                  → send text to Gemini (via C6 http_post) → reply
//                  → TTS reply via Google Cloud TTS → MP3
//                  → pm_audio_play MP3
//
//  Three external dependencies, all of which gate full op:
//    1. C6 http_post / http_response (same as terminal/baseball)
//    2. Google Cloud API key in secrets.h (separate from Gemini key)
//    3. pm_audio MP3 decoder (currently stubbed in pm_audio.c)
//
//  In keyboard mode (no Cloud key), the app falls back to text-only
//  Gemini chat. Same as the S3 v1.1.1 behavior.
// ============================================================

#ifndef PM_APP_VOICE_TERMINAL_H
#define PM_APP_VOICE_TERMINAL_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_voice_terminal(void);
void pm_app_voice_terminal_record_press(void);
void pm_app_voice_terminal_record_release(void);
void pm_app_voice_terminal_toggle_tts(void);
void pm_app_voice_terminal_toggle_keyboard_mode(void);
#ifdef __cplusplus
}
#endif
#endif
