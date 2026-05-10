// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_lora_voice.h — Push-to-talk over LoRa
//
//  P4 architecture: LoRa (SX1262) lives on the wireless module
//  slot on the P4 itself — NOT on the C6. So this app drives
//  the radio directly through P4 SPI.
//
//  Audio chain (P4-side):
//    NS4168 mic in → pm_audio_record_drive() → 8kHz mono PCM
//      → Codec2 encoder (3200bps, 44-byte frames)
//      → SX1262 FSK packets
//      → SX1262 RX → Codec2 decoder → NS4168 speaker out
//
//  S3 quirk that's GONE on P4: GPIO0/trackball-click conflict
//  with ES7210 mic. P4 uses NS4168 with no such overlap.
//
//  SPI Treaty: while voice is active, wardrive SD writes
//  yield. pm_app_lora_voice_active() exposes the flag.
//
//  Status: scaffolded, audio + radio TODOs marked. Same shape
//  as the S3 lora_voice.cpp, ported to ESP-IDF C.
// ============================================================

#ifndef PM_APP_LORA_VOICE_H
#define PM_APP_LORA_VOICE_H

#include "pm_app.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

const pm_app_t* pm_app_lora_voice(void);

bool pm_app_lora_voice_active(void);   // SPI Treaty / wardrive consult

void pm_app_lora_voice_ptt_press(void);
void pm_app_lora_voice_ptt_release(void);

#ifdef __cplusplus
}
#endif

#endif
