<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Pisces Moon OS — Phase 16 Changelog

**Phase:** 16 "Live Iron"
**Date:** 2026-05-26
**Base:** v1.2.0-alpha Phase 15 "Modular Peers"
**Status:** Hardware-validated. Boot confirmed on physical silicon.

---

## Headline

Phase 16 is the breakthrough session. **Pisces Moon P4 is now alive on
real hardware.**

Boot screen renders. Splash renders. Launcher loads. Apps launch.
The M5 Cardputer ADV is wired to the P4 over physical UART1 and the
system treats it as a live radio/input/GPS bridge. Wardrive runs WiFi
scans through the onboard C6 (ESP-Hosted SDIO) and BLE scans through
the Cardputer simultaneously, and continues running in the background
while the user navigates to other apps.

This is no longer bring-up. This is stabilization.

---

## What Changed

### Hardware Architecture Update

The **M5 Cardputer ADV** (ESP32-S3) is now the primary radio and input
bridge for the P4 in the current development configuration. It connects
via a physical 4-wire UART1 header on the CrowPanel Advanced board:

| Signal | P4 GPIO |
|--------|---------|
| RX1    | GPIO48  |
| TX1    | GPIO47  |
| Baud   | 921600  |
| Ground | shared  |

The Cardputer handles:
- **Keyboard input** — full QWERTY, forwarded to P4 via `PMU1 KEY ...`
- **GPS** — BN-180 on the Cardputer, valid fixes forwarded via `PMU1 GPS ...`
- **BLE scanning** — active/passive scan, results via `PMU1 BLE ...`
- **LoRa** (declared, firmware validation pending)
- **WiFi promiscuous** (declared, firmware validation pending)

The standalone BN-180 GPS wired to P4 IO52 is **parked** for this
build phase. It remains in the codebase (commented out) as a future
bench experiment. GPS is now a networked service from the Cardputer.

The onboard ESP32-C6 (Ghost Engine, ESP-Hosted SDIO) continues to
handle WiFi station and scanning for Wardrive.

### New Component: `pm_cardputer_i2c`

Despite the legacy name (a relic of an earlier I2C transport experiment),
this component implements the **UART1 bridge protocol** between the P4
and the Cardputer ADV. It is the spine of the multi-device architecture.

Protocol lines the Cardputer sends:
```
PMU1 HELLO caps=...
PMU1 GPS lat=... lon=... alt=... fix=...
PMU1 KEY code=... mod=...
PMU1 BLE mac=XX:XX:XX:XX:XX:XX rssi=-NN name='...'
PMU1 LORA rssi=-NN snr_x4=N freq=906875 data=HEX
PMU1 WF type=N ch=N rssi=-NN mac=AABBCCDDEEFF len=N data=HEX
```

Commands the P4 sends to the Cardputer:
```
PMU1 PING
PMU1 CMD ble_scan_start active=0 interval=100 window=80
PMU1 CMD ble_scan_stop
PMU1 CMD lora_mesh_start ch=0 freq=906875
PMU1 CMD lora_tx len=N data=HEX
PMU1 CMD lora_stop
PMU1 CMD wifi_promisc_start ch=N filter=N
PMU1 CMD wifi_promisc_stop
```

Confirmed working: `HELLO`, `KEY`, `GPS`, `BLE`.
Pending Cardputer firmware validation: `LORA`, `WF`.

The Cardputer peer registers in `pm_peer` with capabilities:
```
gps_remote, gps_status, lora_tx, lora_rx, lora_mesh,
wifi_scan, wifi_capture, wifi_promisc, ble_scan, ble_gatt,
keyboard_hid, s3_module
```

### Wardrive Redesign

`pm_app_wardrive.c` was redesigned end-to-end against the new 1024×600
Dashboard layout pattern established in the UI redesign specification.

Key changes:
- **Lazy-build on first enter.** DB initialization no longer runs in
  `_enter()`. The previous behavior caused a 6-second freeze on every
  Wardrive open due to synchronous SD operations (mkdir, file create,
  SQLite schema, metadata INSERT) at 10 MHz SDMMC. Init is now deferred
  to scan-start.
- **CSV-first storage.** `s_csv_fallback` defaults to `true`. SQLite
  remains available opt-in but is not the hot path. Per-record SQLite
  overhead (SQL parse + journal + page + fsync) was 100–500 ms on this
  SD card — unacceptable for a firehose logger.
- **Dual-radio BLE.** BLE source selection now prefers the Cardputer ADV
  when present. This offloads BLE scanning from the C6, keeping local
  Bluetooth and audio bandwidth free.
- **Background multitasking.** Wardrive continues running after the user
  leaves the app. The background worker is pinned to OS Core (HP Core 0).
  Foreground state is tracked separately via `s_foreground`; capture
  state via `s_running`. Leaving the app does not stop the scan.
- **Dual-resolution layouts.** Compile-time optimized layouts for both
  supported board sizes:
  - 7-inch (1024×600): 10 network rows, 12 log rows
  - 5-inch (800×480): 7 network rows, 8 log rows

### Boot Screen and Splash (`pm_boot`)

New component added this phase:
- `components/pm_boot/pm_boot.h`
- `components/pm_boot/pm_boot.c`
- `components/pm_boot/CMakeLists.txt`

Implements a DOS POST-style boot status screen followed by a cyberpunk
splash screen. Both render on physical hardware. The splash shows the
ESP32-P4 chip icon, "PISCES MOON / the OS" title, taglines, and
watermark. Confirmed via photo on the 7-inch CrowPanel.

### Backlight Pulse Root Cause Eliminated

The periodic backlight pulse that appeared under render load was traced
to `_boot_visual_probe()` in `main/main.c`. The function was
reconfiguring `PM_PIN_LCD_BL` (the LEDC PWM channel) as a plain GPIO
output, corrupting the LEDC timer state. This manifested as timer drift
visible as a backlight pulse.

Fix: function body neutralized to `(void)on; return;`. The 8-second
heartbeat loop was also removed and replaced with `pm_boot` calls.

### Snake Watchdog Fix

Opening Snake after Wardrive had been running in the background was
triggering task watchdog warnings. Stack traces pointed into LVGL object
creation (`taskLVGL → lv_obj_create`).

Root cause: Snake was building a full on-screen virtual gamepad overlay
during app init, regardless of whether a physical keyboard was connected.
With Wardrive's background worker active on Core 0, LVGL object creation
on Core 1 was hitting the watchdog.

Fix: The overlay is now conditional. When the Cardputer UART link is
live, Snake uses physical keyboard input and skips the overlay. The
overlay remains as fallback for non-Cardputer use.

`pm_apps_games` now declares a dependency on `pm_cardputer_i2c`.

### Mesh Messenger LoRa Fallback

`pm_app_mesh_messenger.c` previously logged:
```
PM_MESH: no LoRa mesh peer available; local radio is none
```

Because there is no direct SX1262 on the P4 board (wireless modules not
yet arrived), and the Cardputer peer was being acquired exclusively by
Wardrive BLE, blocking the fallback path.

Fix: Mesh now falls back to the **shared** Cardputer bridge when no local
SX1262 is detected. It uses `PM_PEER_ROLE_PRIMARY / ANY` rather than
exclusive acquisition. The fallback checks:
1. Cardputer UART link is live
2. Cardputer caps include `PM_CARDPUTER_CAP_LORA`
3. `lora_mesh_start` command succeeds

Expected logs when working:
```
PM_MESH: no local SX1262; trying Cardputer LoRa fallback
PM_CARDPUTER: Cardputer LoRa mesh start ch=0 freq=906875 kHz
PM_MESH: mesh mode active on shared Cardputer ADV, LongFast ch0
```

If logs appear but no RF traffic flows, the remaining issue is
Cardputer firmware-side handling of `lora_mesh_start` and `lora_tx`.

### T-Beam One-Shot Probe

`pm_tbeam.c` previously polled UART2 every 3 seconds regardless of
whether a T-Beam was present. This caused spurious log noise and
wasted cycles.

Fix: `pm_tbeam_init()` now probes once at boot, waits 3 seconds for
a response, and if none arrives: logs "T-Beam not present," sets
`s_uart_open = false`, deletes the UART driver, and returns false.
No further polling. Hot-plug re-detection requires reboot.

### Bug Fixes (12 classes)

1. `_boot_visual_probe` stealing LEDC pin — neutralized
2. 8-second heartbeat loop at boot — removed
3. Wardrive 6-second freeze on open — deferred DB init to scan-start
4. SQLite per-record overhead in wardrive — CSV-first default
5. Brace-less `_enter` functions in 15 apps — fixed with lazy-build guard
6. Missing `esp_wifi` / `esp_event` in `pm_apps_cyber` CMakeLists — added
7. Missing `pm_boot` in `main` CMakeLists — added
8. Mangled multi-line C string literals from shell heredoc — rewritten
9. C++ lambda syntax in `.c` file — replaced with named static stubs
10. Missing forward declarations in wardrive — added
11. T-Beam continuous polling when absent — one-shot probe
12. Missing Montserrat font sizes in sdkconfig — enabled 10, 12, 16, 18, 20, 24, 36

### Layout Constants Header

New file: `components/pm_ui/include/pm_ui_p4.h`

Canonical layout constants for the 1024×600 canvas, derived from 10
reference HTML app designs. Wardrive is the first app to use this
header as its layout template. All future app UI redesigns follow the
same pattern.

### Dual Board Build

Both P4 board sizes now compile cleanly:

| Profile | Resolution | Build dir | Binary size |
|---------|-----------|-----------|-------------|
| `elecrow_p4_7` (default) | 1024×600 | `build/` | ~2.27 MB, 64% flash free |
| `elecrow_p4_5` | 800×480 | `build-p4-5/` | ~2.27 MB, 64% flash free |

Build commands:
```sh
# 7-inch (default)
idf.py build

# 5-inch
PM_P4_BOARD=elecrow_p4_5 idf.py -B build-p4-5 build
```

Flash commands:
```sh
# 7-inch
PORT=$(ls /dev/cu.wchusbserial* | head -1)
idf.py -p "$PORT" app-flash monitor

# 5-inch
PORT=$(ls /dev/cu.wchusbserial* | head -1)
idf.py -B build-p4-5 -p "$PORT" app-flash monitor
```

---

## Confirmed Working on Hardware

- ✅ P4 boots cleanly to launcher
- ✅ Boot screen (POST-style) renders on physical silicon
- ✅ Cyberpunk splash renders on physical silicon
- ✅ Launcher loads after splash
- ✅ Cardputer ADV UART bridge detected at boot
- ✅ Cardputer keyboard input forwarded to P4
- ✅ Cardputer GPS provides valid fixes
- ✅ Cardputer BLE scanning active
- ✅ Wardrive: WiFi scan via C6/ESP-Hosted
- ✅ Wardrive: BLE scan via Cardputer
- ✅ Wardrive continues running in background after leaving app
- ✅ Notepad accepts keyboard input, saves to SD
- ✅ Snake launches without watchdog with Cardputer connected
- ✅ 5-inch and 7-inch builds both compile

---

## Pending Validation

- 🔲 Cardputer firmware LoRa command handling (`lora_mesh_start`, `lora_tx`, `lora_stop`)
- 🔲 Cardputer WiFi promiscuous frame forwarding (`PMU1 WF ...`)
- 🔲 Mesh LoRa fallback RF traffic (P4 side coded; Cardputer firmware side unconfirmed)
- 🔲 Rename `pm_cardputer_i2c` log strings from "I2C Module" to "UART Bridge" (cosmetic)

---

## Incoming Hardware

Wireless module order placed (not yet arrived):
- SX1262 LoRa
- ESP32-H2
- ESP32-C6 plug-in (for custom firmware development)
- nRF24L01

When modules arrive, `pm_radio_init_auto()` detects them at boot.
LoRa mesh and promiscuous capture will no longer depend on the
Cardputer fallback path.

---

## Files Changed This Phase

### Added
```
components/pm_boot/include/pm_boot.h
components/pm_boot/pm_boot.c
components/pm_boot/CMakeLists.txt
components/pm_cardputer_i2c/include/pm_cardputer_i2c.h
components/pm_cardputer_i2c/pm_cardputer_i2c.c
components/pm_cardputer_i2c/CMakeLists.txt
components/pm_ui/include/pm_ui_p4.h
```

### Significantly Modified
```
main/main.c
components/pm_apps/pm_apps_cyber/pm_app_wardrive.c
components/pm_apps/pm_apps_cyber/CMakeLists.txt
components/pm_apps/pm_apps_games/pm_app_snake.c
components/pm_apps/pm_apps_games/CMakeLists.txt
components/pm_apps/pm_apps_comms/pm_app_mesh_messenger.c
components/pm_tbeam/pm_tbeam.c
sdkconfig.defaults
```

### Patched (brace-less `_enter` fix + lazy-build guard)
```
components/pm_apps/pm_apps_cyber/pm_app_rf_spectrum.c
+ 14 additional app files across categories
```

---

## Memory Budget

From ESP-IDF v5.5.3 build summary:

| Region | Used | Free | Total |
|--------|------|------|-------|
| External RAM (PSRAM) | 1.72 MB | ~30 MB | 32 MB physical |
| DIRAM (internal SRAM) | 334 KB (75%) | 111 KB | 445 KB |
| HP Core RAM | 104 B | ~8 KB | 8 KB |
| LP RAM | 56 B | ~32 KB | 32 KB |

Binary: ~1.85 MB. PSRAM headroom is wide open for audio, reading apps,
and future video work. Internal SRAM at 75% — future subsystems should
prefer PSRAM allocation where possible. LP RAM wide open for Phase 17
Software PMU.

---

## Architecture Conventions (Locked)

These do not change without a documented reason:

| Core | Role |
|------|------|
| ESP32-C6 (separate chip, SDIO) | Ghost Engine — Core 0 designation |
| P4 HP Core 0 | OS Core |
| P4 HP Core 1 | UI Core (LVGL pinned) |
| P4 LP Core | Sentinel Core (future Software PMU) |

- **ESP-Hosted over SDIO** is the C6 transport. The S3-era UART-based
  `pm_c6_bridge` remains in `#if 0` blocks as reference only.
- **Apps lazy-initialize on first enter**, never at registration.
- **Modular peripherals probe once at boot.** No continuous polling.
  Absent hardware shuts down cleanly. Hot-plug requires reboot.
- **CSV/JSONL for firehose logging.** SQLite for queryable user data only.

---

## What's Next (Phase 17)

1. Validate Cardputer LoRa firmware command handling
2. Validate Cardputer WiFi promiscuous forwarding
3. Status bar overlay
4. Pull-down control center
5. App switcher
6. `pm_sdlog` queued CSV writer task
7. Software PMU on LP Core
8. Per-app UI redesigns following the wardrive Dashboard template
9. Wireless module bring-up on hardware arrival

---

## Credits

Eric Becker / Fluid Fortune — architecture, hardware integration,
multi-device design, all session work.

fluidfortune.com
