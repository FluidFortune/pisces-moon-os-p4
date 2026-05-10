// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_audio.h — Audio HAL for the NS4168 codec on the
//                CrowPanel Advanced 7" P4 board.
//
//  The board has:
//    - NS4168 audio codec (output amp)
//    - Dual mic array (input)
//    - Dual speaker outputs
//
//  The ESP-IDF managed component "espressif/esp_codec_dev"
//  exposes a uniform API for codecs of this class. This
//  module wraps it so apps don't link directly against
//  esp_codec_dev — keeps the audio surface small.
//
//  Apps:
//    - pm_app_audio_player    uses pm_audio_play_*
//    - pm_app_audio_recorder  uses pm_audio_record_*
//    - Future TTS / voice term will share both
//
//  Concurrency: only ONE direction (play OR record) is
//  supported at a time. The lock is internal — opening
//  the second direction returns false until the first is
//  released. (Full-duplex with the same codec needs a
//  second I2S port and is a future improvement.)
//
//  All file I/O wraps the SPI Treaty mutex inside the
//  player/recorder code so apps don't see it.
// ============================================================

#ifndef PM_AUDIO_H
#define PM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_AUDIO_FMT_WAV  = 1,     // raw PCM with WAV header
    PM_AUDIO_FMT_MP3  = 2,     // decoded via ESP-IDF audio_pipeline / mp3_decoder
    PM_AUDIO_FMT_FLAC = 3,
    PM_AUDIO_FMT_AAC  = 4,
    PM_AUDIO_FMT_OGG  = 5,
    PM_AUDIO_FMT_UNKNOWN = 0,
} pm_audio_format_t;

// Init the codec. Must be called once at boot or first use.
// Returns false if init fails (codec ack absent — board unwired
// or first-bring-up).
bool pm_audio_init(void);
void pm_audio_deinit(void);

// ── Playback ─────────────────────────────────────────────────
// Open a file for playback. format auto-detected from extension
// if PM_AUDIO_FMT_UNKNOWN is passed.
bool pm_audio_play_open(const char* path, pm_audio_format_t fmt);

// Drive playback — call frequently from the app's tick().
// Returns true while playing, false when EOF or stopped.
bool pm_audio_play_drive(void);

void pm_audio_play_pause(bool paused);
void pm_audio_play_stop(void);

bool pm_audio_play_is_playing(void);
bool pm_audio_play_is_paused(void);
uint32_t pm_audio_play_position_ms(void);
uint32_t pm_audio_play_duration_ms(void);

// Volume 0..21 (kept compatible with S3 player).
void pm_audio_set_volume(int v);
int  pm_audio_get_volume(void);

// VU-style peak (0..255), updated as the engine runs. Useful for
// the visualizer.
uint8_t pm_audio_peak(void);

// ── Recording ────────────────────────────────────────────────
// Start recording 16kHz 16-bit mono WAV to `path`. File written
// using SPI Treaty discipline. Returns false if codec/mic open
// fails or playback is in progress.
bool     pm_audio_record_start(const char* path);

// Drive recording — call from app tick(). Reads I2S into PSRAM
// buffer, flushes to SD periodically. Returns false on error.
bool     pm_audio_record_drive(void);

// Auto-stop after this many ms (default: 5 minutes). Set 0 to
// disable.
void     pm_audio_record_set_max_ms(uint32_t max_ms);

void     pm_audio_record_stop(void);
bool     pm_audio_record_is_recording(void);
uint32_t pm_audio_record_bytes(void);
uint32_t pm_audio_record_elapsed_ms(void);
uint8_t  pm_audio_record_peak(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_AUDIO_H
