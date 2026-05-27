// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_audio_recorder.h — Voice recorder
//
//  16 kHz mono WAV → /sd/recordings/rec_NNN.wav
//  Auto-numbered. Auto-stop at 5 minutes. Live peak meter.
//
//  P4 dual-mic note: NS4168 + dual mic array supports beam-
//  forming. For now we record from one channel; a future
//  enhancement can fold both mics with a stereo WAV or
//  beamformed mono.
// ============================================================

#ifndef PM_APP_AUDIO_RECORDER_H
#define PM_APP_AUDIO_RECORDER_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_audio_recorder(void);
void pm_app_audio_recorder_toggle(void);
void pm_app_audio_recorder_delete_at_cursor(void);
#ifdef __cplusplus
}
#endif
#endif
