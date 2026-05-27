// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_c6_programmer.h — In-system flasher for the onboard C6
//
//  The CrowPanel Advanced 7" routes the ESP32-C6 coprocessor
//  to the P4 over SDIO (the standard esp_hosted topology).
//  ELECROW notes that the C6 firmware "is pre-programmed at
//  the factory and cannot be directly reprogrammed via P4"
//  (default), but they also publish a "Guide to Upgrading the
//  C6 Firmware Using the ESP32-P4 Chip" — meaning the SDIO
//  flash path IS supported, just not exposed in default tools.
//
//  This component implements that path. The P4 acts as a
//  programmer: it controls C6 BOOT/EN via SDIO sideband lines,
//  drops the C6 into bootloader mode, and pushes firmware
//  blocks via the ESP serial bootloader protocol tunneled
//  through SDIO.
//
//  Bootloader protocol reference:
//    https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/serial-protocol.html
//
//  Why this matters for Pisces Moon:
//    The onboard C6 is the host of the Ghost Engine. To run
//    custom Ghost firmware, the user must reflash the C6.
//    Without this component, that requires a specialized
//    cable + esptool; with it, the user just opens the C6
//    Flasher app on the P4, points it at a firmware blob on
//    SD, and presses FLASH.
//
//  Status: scaffold. The SDIO transport details require
//  testing on real hardware — packet framing varies slightly
//  between ELECROW's stock loader and stock ESP32 ROM. Phase
//  13 lays out the API and protocol logic; Phase 14 will be
//  the bring-up and tuning pass once a board is ready.
// ============================================================

#ifndef PM_C6_PROGRAMMER_H
#define PM_C6_PROGRAMMER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Status codes ─────────────────────────────────────────────
typedef enum {
    PM_C6PROG_OK              =  0,
    PM_C6PROG_ERR             = -1,
    PM_C6PROG_NOT_INIT        = -2,
    PM_C6PROG_NO_SYNC         = -3,    // C6 didn't enter bootloader
    PM_C6PROG_BAD_RESPONSE    = -4,    // protocol handshake failed
    PM_C6PROG_TIMEOUT         = -5,
    PM_C6PROG_FLASH_WRITE_ERR = -6,
    PM_C6PROG_FLASH_VERIFY_ERR= -7,
    PM_C6PROG_FILE_OPEN_ERR   = -8,
    PM_C6PROG_FILE_READ_ERR   = -9,
    PM_C6PROG_BAD_PARAM       = -10,
} pm_c6prog_status_t;

// ── Progress reporting ──────────────────────────────────────
//
// Called from the worker task during a flash session. UI apps
// register a callback to drive a progress bar.
typedef enum {
    PM_C6PROG_PHASE_RESET,        // entering bootloader
    PM_C6PROG_PHASE_SYNC,         // SYNC handshake
    PM_C6PROG_PHASE_ERASE,        // FLASH_BEGIN (implicit erase)
    PM_C6PROG_PHASE_WRITE,        // FLASH_DATA blocks streaming
    PM_C6PROG_PHASE_VERIFY,       // optional MD5 verify
    PM_C6PROG_PHASE_REBOOT,       // FLASH_END + reboot
    PM_C6PROG_PHASE_DONE,
    PM_C6PROG_PHASE_FAILED,
} pm_c6prog_phase_t;

typedef void (*pm_c6prog_progress_cb_t)(pm_c6prog_phase_t phase,
                                          uint32_t bytes_done,
                                          uint32_t bytes_total,
                                          const char* message,
                                          void* user);

// ── Init ─────────────────────────────────────────────────────
// Sets up the SDIO transport in "programmer mode" — distinct
// from the runtime esp_hosted bridge. This may need to release
// any active esp_hosted session first; the function returns
// an error if the bridge can't be temporarily torn down.
esp_err_t pm_c6_programmer_init(void);
void      pm_c6_programmer_deinit(void);

// ── Entry / control ─────────────────────────────────────────
// Pulls C6 into bootloader: assert BOOT pin, toggle EN.
// Returns OK if bootloader responded to SYNC.
pm_c6prog_status_t pm_c6_programmer_enter_bootloader(void);

// Drop C6 back to running firmware.
pm_c6prog_status_t pm_c6_programmer_run_firmware(void);

// ── Flash a binary from disk ────────────────────────────────
//
// Path is on the P4's SD card mount, e.g. "/sd/c6_firmware.bin".
// flash_offset is where in C6 flash to write — typically 0x10000
// for the application image, 0x0 for full flash.
//
// Calls progress_cb periodically; user is opaque pointer.
// This is a blocking call that may take 30+ seconds for a 1MB
// firmware. Run from a dedicated task, not a UI handler.
pm_c6prog_status_t pm_c6_programmer_flash_file(const char*   path,
                                                  uint32_t      flash_offset,
                                                  bool           verify_md5,
                                                  pm_c6prog_progress_cb_t cb,
                                                  void*          user);

// Convenience: flash from RAM buffer (for embedded firmware blobs).
pm_c6prog_status_t pm_c6_programmer_flash_buffer(const uint8_t* data,
                                                    size_t        len,
                                                    uint32_t      flash_offset,
                                                    bool           verify_md5,
                                                    pm_c6prog_progress_cb_t cb,
                                                    void*          user);

// ── Diagnostics ─────────────────────────────────────────────
// After enter_bootloader, returns the chip's reported MAC and
// chip-id from READ_REG calls. Useful for sanity-checking that
// we really are talking to a C6 and not garbage on the line.
typedef struct {
    uint32_t chip_id;
    uint8_t  mac[6];
    uint32_t flash_size_bytes;
    bool     valid;
} pm_c6prog_chip_info_t;

pm_c6prog_status_t pm_c6_programmer_chip_info(pm_c6prog_chip_info_t* out);

// Status as a human-readable string for logs/UI.
const char* pm_c6_programmer_status_str(pm_c6prog_status_t s);

#ifdef __cplusplus
}
#endif

#endif  // PM_C6_PROGRAMMER_H
