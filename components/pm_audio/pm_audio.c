// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_audio.c — Audio HAL implementation (Phase-6 scaffold)
//
//  Codec wiring is documented but not active until the BSP
//  pulls in esp_codec_dev and ties NS4168 + dual mic to the
//  exact GPIO pins from the Eagle schematic.
//
//  Until then, this module:
//    - keeps state machine bookkeeping correct
//    - exposes the API apps will call
//    - performs SPI-Treaty-wrapped file I/O for WAV record
//    - provides a simulated peak meter (for UI development)
//    - logs what would happen on real hardware
//
//  When BSP wiring is added, the ENGINE_TODO blocks below
//  become real I2S DMA and mp3 decoder calls — the API and
//  state machine do not change.
// ============================================================

#include "pm_audio.h"
#include "pm_hal.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

static const char* TAG = "PM_AUDIO";

// WAV header — kept compatible with S3 audio_recorder.cpp
typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t fileSize;
    char     wave[4];
    char     fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char     data[4];
    uint32_t dataSize;
} pm_wav_header_t;

#define REC_SAMPLE_RATE   16000
#define REC_BITS          16
#define REC_CHANNELS      1
#define REC_BUF_BYTES     (8 * 1024)
#define REC_FLUSH_EVERY   32
#define DEFAULT_MAX_REC_MS  (5 * 60 * 1000UL)

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
typedef enum { ST_IDLE, ST_PLAYING, ST_RECORDING } state_t;

static state_t  s_state = ST_IDLE;
static bool     s_inited = false;

// Volume / peak (shared between play/record for UI)
static int      s_volume = 12;
static uint8_t  s_peak   = 0;

// Playback state
static char     s_play_path[160] = "";
static pm_audio_format_t s_play_fmt = PM_AUDIO_FMT_UNKNOWN;
static bool     s_play_paused = false;
static uint32_t s_play_start_ms = 0;
static uint32_t s_play_pause_total_ms = 0;
static uint32_t s_play_pause_began_ms = 0;
static uint32_t s_play_duration_ms = 0;     // best-effort estimate
static pm_file_t* s_play_file = NULL;

// Recording state
static char     s_rec_path[160] = "";
static pm_file_t* s_rec_file = NULL;
static uint32_t s_rec_bytes = 0;
static uint32_t s_rec_start_ms = 0;
static uint32_t s_rec_max_ms = DEFAULT_MAX_REC_MS;
static int      s_rec_flush_count = 0;
static uint8_t* s_rec_buf = NULL;       // PSRAM
static int      s_rec_simulated_phase = 0;

// ─────────────────────────────────────────────
//  Format detection
// ─────────────────────────────────────────────
static pm_audio_format_t _detect_format(const char* path) {
    if (!path) return PM_AUDIO_FMT_UNKNOWN;
    size_t n = strlen(path);
    if (n < 4) return PM_AUDIO_FMT_UNKNOWN;
    const char* ext4 = path + n - 4;
    const char* ext5 = (n >= 5) ? path + n - 5 : NULL;
    if (strcasecmp(ext4, ".wav")  == 0) return PM_AUDIO_FMT_WAV;
    if (strcasecmp(ext4, ".mp3")  == 0) return PM_AUDIO_FMT_MP3;
    if (ext5 && strcasecmp(ext5, ".flac") == 0) return PM_AUDIO_FMT_FLAC;
    if (strcasecmp(ext4, ".aac")  == 0) return PM_AUDIO_FMT_AAC;
    if (strcasecmp(ext4, ".ogg")  == 0) return PM_AUDIO_FMT_OGG;
    return PM_AUDIO_FMT_UNKNOWN;
}

// ─────────────────────────────────────────────
//  Init / deinit
// ─────────────────────────────────────────────
bool pm_audio_init(void) {
    if (s_inited) return true;

    // ENGINE_TODO:
    //   - Acquire I2S0 host (audio out — NS4168) with DMA buffers.
    //   - Acquire I2S1 host (mic array in) with DMA buffers.
    //   - Init esp_codec_dev for NS4168 over I2C control bus.
    //   - Set initial volume (s_volume mapped to codec range).
    // For now, mark inited so the API is callable for UI dev.
    s_inited = true;
    pm_log_i(TAG, "pm_audio init (BSP wiring pending — UI-only mode)");
    return true;
}

void pm_audio_deinit(void) {
    if (!s_inited) return;
    pm_audio_play_stop();
    pm_audio_record_stop();
    // ENGINE_TODO: tear down I2S0/I2S1, release codec.
    s_inited = false;
}

// ─────────────────────────────────────────────
//  Volume / peak
// ─────────────────────────────────────────────
void pm_audio_set_volume(int v) {
    if (v < 0) v = 0;
    if (v > 21) v = 21;
    s_volume = v;
    // ENGINE_TODO: codec set volume = v / 21.0 mapped to dB.
    pm_log_d(TAG, "volume=%d", v);
}

int  pm_audio_get_volume(void) { return s_volume; }
uint8_t pm_audio_peak(void)    { return s_peak; }

// ─────────────────────────────────────────────
//  Playback
// ─────────────────────────────────────────────
bool pm_audio_play_open(const char* path, pm_audio_format_t fmt) {
    if (!s_inited) pm_audio_init();
    if (s_state == ST_RECORDING) {
        pm_log_w(TAG, "cannot play while recording");
        return false;
    }
    if (s_state == ST_PLAYING) pm_audio_play_stop();

    if (fmt == PM_AUDIO_FMT_UNKNOWN) fmt = _detect_format(path);
    if (fmt == PM_AUDIO_FMT_UNKNOWN) {
        pm_log_w(TAG, "unsupported format: %s", path ? path : "(null)");
        return false;
    }

    pm_file_t* f = NULL;
    PM_SPI_TAKE("audio_play_open") {
        f = pm_file_open(path, PM_FILE_READ);
    } PM_SPI_GIVE();
    if (!f) {
        pm_log_w(TAG, "open failed: %s", path);
        return false;
    }

    strncpy(s_play_path, path, sizeof(s_play_path) - 1);
    s_play_path[sizeof(s_play_path) - 1] = 0;
    s_play_fmt    = fmt;
    s_play_file   = f;
    s_play_paused = false;
    s_play_start_ms = pm_millis();
    s_play_pause_total_ms = 0;
    s_play_pause_began_ms = 0;
    s_play_duration_ms = 0;     // unknown until decoder reports
    s_state       = ST_PLAYING;

    // ENGINE_TODO: instantiate decoder pipeline for `fmt`.
    //   WAV  : parse header, set sample rate/bits, route raw to I2S0.
    //   MP3  : audio_pipeline + mp3_decoder + i2s_writer.
    //   FLAC/AAC/OGG: same shape, swap decoder element.
    pm_log_i(TAG, "play '%s' fmt=%d", path, (int)fmt);
    return true;
}

bool pm_audio_play_drive(void) {
    if (s_state != ST_PLAYING) return false;
    if (s_play_paused)         return true;

    // ENGINE_TODO: pump the audio pipeline; check decoder state for EOF.
    // Until engine is live, simulate a 1-minute "track" so UI can be
    // exercised. Once a real decoder is in, replace the eof check.
    uint32_t elapsed = pm_audio_play_position_ms();
    if (elapsed > 60000) {
        pm_log_i(TAG, "[sim] play EOF reached");
        pm_audio_play_stop();
        return false;
    }

    // Simulated peak (seeded from elapsed)
    s_peak = (uint8_t)((elapsed * 7) % 255);
    return true;
}

void pm_audio_play_pause(bool paused) {
    if (s_state != ST_PLAYING) return;
    if (paused == s_play_paused) return;
    s_play_paused = paused;
    if (paused) {
        s_play_pause_began_ms = pm_millis();
        // ENGINE_TODO: pause pipeline.
    } else {
        s_play_pause_total_ms += pm_millis() - s_play_pause_began_ms;
        // ENGINE_TODO: resume pipeline.
    }
}

void pm_audio_play_stop(void) {
    if (s_state != ST_PLAYING) return;
    if (s_play_file) {
        PM_SPI_TAKE("audio_play_close") {
            pm_file_close(s_play_file);
        } PM_SPI_GIVE();
        s_play_file = NULL;
    }
    // ENGINE_TODO: stop pipeline + free decoder.
    s_state       = ST_IDLE;
    s_play_paused = false;
    s_peak        = 0;
    pm_log_i(TAG, "play stop");
}

bool pm_audio_play_is_playing(void) { return s_state == ST_PLAYING; }
bool pm_audio_play_is_paused(void)  { return s_state == ST_PLAYING && s_play_paused; }

uint32_t pm_audio_play_position_ms(void) {
    if (s_state != ST_PLAYING) return 0;
    uint32_t now = pm_millis();
    uint32_t elapsed = now - s_play_start_ms - s_play_pause_total_ms;
    if (s_play_paused) elapsed -= (now - s_play_pause_began_ms);
    return elapsed;
}

uint32_t pm_audio_play_duration_ms(void) { return s_play_duration_ms; }

// ─────────────────────────────────────────────
//  Recording
// ─────────────────────────────────────────────
static bool _ensure_rec_buf(void) {
    if (s_rec_buf) return true;
    s_rec_buf = (uint8_t*)pm_psram_alloc(REC_BUF_BYTES);
    if (!s_rec_buf) {
        pm_log_e(TAG, "rec buf alloc failed");
        return false;
    }
    return true;
}

bool pm_audio_record_start(const char* path) {
    if (!s_inited) pm_audio_init();
    if (s_state == ST_PLAYING) {
        pm_log_w(TAG, "cannot record while playing");
        return false;
    }
    if (s_state == ST_RECORDING) pm_audio_record_stop();
    if (!_ensure_rec_buf()) return false;

    pm_file_t* f = NULL;
    PM_SPI_TAKE("audio_rec_open") {
        pm_file_mkdir("/sd/recordings");
        f = pm_file_open(path,
                          PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
        if (f) {
            // Write placeholder header — patched on stop
            pm_wav_header_t hdr = {0};
            memcpy(hdr.riff, "RIFF", 4);
            memcpy(hdr.wave, "WAVE", 4);
            memcpy(hdr.fmt,  "fmt ", 4);
            hdr.fmtSize       = 16;
            hdr.audioFormat   = 1;
            hdr.numChannels   = REC_CHANNELS;
            hdr.sampleRate    = REC_SAMPLE_RATE;
            hdr.byteRate      = REC_SAMPLE_RATE * REC_CHANNELS * (REC_BITS / 8);
            hdr.blockAlign    = REC_CHANNELS * (REC_BITS / 8);
            hdr.bitsPerSample = REC_BITS;
            memcpy(hdr.data, "data", 4);
            hdr.dataSize = 0;
            hdr.fileSize = sizeof(hdr) - 8;
            pm_file_write(f, &hdr, sizeof(hdr));
        }
    } PM_SPI_GIVE();
    if (!f) {
        pm_log_w(TAG, "record open failed: %s", path);
        return false;
    }

    strncpy(s_rec_path, path, sizeof(s_rec_path) - 1);
    s_rec_path[sizeof(s_rec_path) - 1] = 0;
    s_rec_file        = f;
    s_rec_bytes       = 0;
    s_rec_start_ms    = pm_millis();
    s_rec_flush_count = 0;
    s_rec_simulated_phase = 0;
    s_state           = ST_RECORDING;

    // ENGINE_TODO:
    //   - Configure I2S1 RX from mic array at REC_SAMPLE_RATE.
    //   - Warmup reads (4 × DMA buffer) to drain stale samples.
    pm_log_i(TAG, "rec start: %s", path);
    return true;
}

bool pm_audio_record_drive(void) {
    if (s_state != ST_RECORDING) return false;
    if (!s_rec_file || !s_rec_buf) return false;

    // Auto-stop?
    uint32_t elapsed = pm_millis() - s_rec_start_ms;
    if (s_rec_max_ms > 0 && elapsed >= s_rec_max_ms) {
        pm_audio_record_stop();
        return false;
    }

    // ENGINE_TODO: i2s_read into s_rec_buf, get bytes_read.
    // For simulation, generate silence at the right rate so the
    // UI can show a meter and file growth.
    int bytes_per_call = REC_SAMPLE_RATE * (REC_BITS / 8) * REC_CHANNELS / 50;
    if (bytes_per_call > REC_BUF_BYTES) bytes_per_call = REC_BUF_BYTES;
    memset(s_rec_buf, 0, bytes_per_call);

    PM_SPI_TAKE("audio_rec_write") {
        size_t wrote = pm_file_write(s_rec_file, s_rec_buf, bytes_per_call);
        s_rec_bytes += (uint32_t)wrote;
        if (++s_rec_flush_count >= REC_FLUSH_EVERY) {
            pm_file_flush(s_rec_file);
            s_rec_flush_count = 0;
        }
    } PM_SPI_GIVE();

    // Simulated peak meter — periodic up/down sweep so UI can be tested
    s_rec_simulated_phase = (s_rec_simulated_phase + 9) % 256;
    s_peak = (uint8_t)s_rec_simulated_phase;
    return true;
}

void pm_audio_record_set_max_ms(uint32_t max_ms) { s_rec_max_ms = max_ms; }

void pm_audio_record_stop(void) {
    if (s_state != ST_RECORDING) return;

    // Patch WAV header
    PM_SPI_TAKE("audio_rec_close") {
        if (s_rec_file) {
            pm_file_flush(s_rec_file);
            uint32_t data_size = s_rec_bytes;
            uint32_t file_size = data_size + sizeof(pm_wav_header_t) - 8;
            pm_file_seek(s_rec_file, 4);
            pm_file_write(s_rec_file, &file_size, 4);
            pm_file_seek(s_rec_file, sizeof(pm_wav_header_t) - 4);
            pm_file_write(s_rec_file, &data_size, 4);
            pm_file_close(s_rec_file);
            s_rec_file = NULL;
        }
    } PM_SPI_GIVE();

    // ENGINE_TODO: stop I2S1, release codec input path.
    pm_log_i(TAG, "rec stop: %u bytes", (unsigned)s_rec_bytes);
    s_state = ST_IDLE;
    s_peak  = 0;
}

bool     pm_audio_record_is_recording(void) { return s_state == ST_RECORDING; }
uint32_t pm_audio_record_bytes(void)        { return s_rec_bytes; }
uint32_t pm_audio_record_elapsed_ms(void)   {
    return s_state == ST_RECORDING ? pm_millis() - s_rec_start_ms : 0;
}
uint8_t  pm_audio_record_peak(void)         { return s_peak; }
