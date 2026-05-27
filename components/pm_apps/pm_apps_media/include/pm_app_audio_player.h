// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_audio_player.h — WinAmp-style audio player
//
//  Scans /sd/music/ (and /sd/audio/, /sd/recordings/) for
//  supported files (WAV/MP3/FLAC/AAC/OGG). Plays via
//  pm_audio_*. Visualizer drives off pm_audio_peak().
//
//  P4 perks vs S3:
//    - 1024×600 means a real WinAmp-grade UI is feasible
//    - 32 MB PSRAM means MP3 decoder buffers don't fight
//      the rest of the OS
// ============================================================

#ifndef PM_APP_AUDIO_PLAYER_H
#define PM_APP_AUDIO_PLAYER_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_audio_player(void);
void pm_app_audio_player_play(int track_idx);
void pm_app_audio_player_next(void);
void pm_app_audio_player_prev(void);
void pm_app_audio_player_toggle_pause(void);
void pm_app_audio_player_volume(int delta);
#ifdef __cplusplus
}
#endif
#endif
