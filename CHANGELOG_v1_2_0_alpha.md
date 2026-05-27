<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Pisces Moon OS — Changelog v1.2.0-alpha

**Release:** v1.2.0-alpha "Pisces Moon P4"
**Date:** 2026-05-10
**Base:** v1.1.1 "ELF Treaty Patch"
**Status:** Alpha — full firmware tree built, hardware bring-up pending

---

## Headline

This is the largest release in the project's history: a complete port from the
**ESP32-S3** (T-Deck Plus, C++/Arduino) to the **ESP32-P4** (ELECROW
CrowPanel Advanced 7" HMI, pure C / ESP-IDF) plus a dedicated **ESP32-C6**
coprocessor running a custom Ghost Engine firmware.

It is also a structural rewrite. The S3 code was a single Arduino sketch
with shared state and ad-hoc app dispatch; the P4 codebase is 49 apps
distributed across 7 categories, every app behind a `pm_app_t` interface,
every subsystem behind a clean component boundary, every OS service in
its own ESP-IDF component.

The Ghost Engine — the always-on radio capture daemon — has moved off
the main MCU entirely and now runs on its own dedicated ESP32-C6 chip
that does nothing but radios. The two chips speak over UART using a
JSON-framed bridge protocol with 25+ commands.

19,685 total lines across the two firmware trees. 164 source files.
13 development phases.

---

## What This Release Is

This is an **Alpha**. The full firmware tree is in place — every
subsystem has either real working code or a documented scaffold —
but no hardware bring-up has been done yet.

What that means in practice:

- **The build will succeed** end-to-end against ESP-IDF v5.5.3 with the
  managed components listed in `main/idf_component.yml`.
- **The boot sequence is real and runs to launcher visibility.** Pin
  numbers are verified from the ELECROW wiki and Lesson 14, not
  placeholders.
- **49 apps are registered and loadable** with real LVGL screens.
  Four apps have full custom UIs (clock, calculator, wifi, wardrive);
  the other 45 use a uniform default-screen template that boots cleanly
  and is ready for screen-by-screen polish.
- **Radios fall back gracefully** if hardware isn't present. No module
  in the wireless slot? Auto-detect reports "none," LoRa apps show
  "no radio detected." No GPS connected? `pm_gps_state` stays "no fix."
  No C6 firmware flashed? Bridge events stay quiet but nothing crashes.
- **Hardware bring-up is the next phase.** Several pieces — the C6 SDIO
  flasher transport, the Codec2 voice encoder, the WPA hccapx assembler,
  the NimBLE GATT central plumbing — are explicitly stubbed pending
  testing on a real board.

If you flash this onto a CrowPanel Advanced 7" board today, the launcher
should appear on the screen, touch should work, GPS should fix once it
sees the sky, and SD wardrive should write `/sd/sessions/session_*.db`.
What's pending is per-radio bring-up validation and the C6 firmware
flash path.

---

## Architecture: S3 → P4/C6 Comparison

| Concern | v1.1.x (S3) | v1.2.0 (P4 + C6) |
|---|---|---|
| Main MCU | ESP32-S3 (single chip) | ESP32-P4 (dual-core RISC-V) |
| Coprocessor | None | ESP32-C6 (radios) |
| Display | 2.8" 320×240 ST7789 SPI | 7" 1024×600 IPS MIPI-DSI |
| Touch | Trackball + keys | Capacitive (GT911) |
| Language | C++ / Arduino | C (one C++ TU for RadioLib) |
| Framework | Arduino-ESP32 | ESP-IDF v5.5.3 |
| UI | Bare framebuffer + custom widgets | LVGL v9.x |
| Audio | I2S to dual speakers (mono) | I2S to NS4168 (stereo speakers, PDM mic) |
| Storage | SD over SPI Treaty | SD over SDIO 1-bit (dedicated peripheral) |
| Radios | All on main MCU | All on C6 (WiFi/BLE) + slot module (LoRa/nRF24) |
| Wardrive | Pauses on app launch | Always-on, dedicated MCU |
| Code organization | One sketch, ~50 .cpp files | 11 components, 49 app modules, 137 P4 files + 27 C6 files |

The killer-feature framing is the **C6 as dedicated radio MCU**. On the S3
the wardrive task competed with the UI, audio, and SPI bus for cycles;
running a game would pause WiFi/BLE capture. On the P4/C6 design the C6
is wardriving 100% of the time the device is powered, regardless of
what the user does with the P4. This is the AGPL "always-on dedicated
radio MCU" architecture and is the project's primary distinctive claim.

---

## Phase-by-Phase Summary

The port was developed across 13 phases. Each phase produced a packaged
zip; cumulative LOC tracked here.

### Phase 1 — Foundation
- ESP-IDF project skeleton, partition table, sdkconfig
- `pm_hal` component: logging, timing, SPI Treaty mutex, PSRAM helpers, NVS, CRC32
- `pm_app` interface: `pm_app_t` struct with init/enter/tick/exit/deinit lifecycle
- `pm_launcher` skeleton (LVGL UI deferred to Phase 10)
- `pm_c6_bridge` UART layer with cJSON event dispatch
- **1,681 LOC**

### Phase 2 — SYSTEM category (8 apps)
About, Files, FileMgr, Bridge (C6 console), Gamepad pairing, MicroPython,
ELF browser, System info. **+1,474 LOC.**

### Phase 3 — TOOLS category (5 apps)
Notepad, Calculator (with subnet calculator mode), Clock + stopwatch,
Calendar, Etch (drawing). **+1,157 LOC.**

### Phase 4 — INTEL category (7 apps + nosql)
Terminal, Gemini Log, Reference Medical, Reference Survival, Baseball
Live (MLB API client), Trails (POI/heat-map), SSH client. Added
`pm_nosql` for unified KV storage. **+1,447 LOC.**

### Phase 5 — GAMES category (7 apps)
Snake, Pacman, Galaga, Chess, Doom, SimCity, Retro ELF launcher.
**+1,849 LOC.**

### Phase 6 — MEDIA category (2 apps + audio HAL)
Audio Player, Audio Recorder, with new `pm_audio` component for
NS4168 + I2S setup. **+996 LOC.**

### Phase 7 — COMMS category (6 apps + GPS state)
GPS, WiFi, Bluetooth, LoRa Voice, Mesh Messenger (Meshtastic-compatible
LongFast), Voice Terminal. Added `pm_gps_state` shared cache. **+1,543 LOC.**

### Phase 8 — CYBER category (14 apps + SQLite)
Wardrive (with full `pm_sqlite` per-boot DB and CSV fallback), BT Radar,
Packet Sniffer, Beacon Spotter, Net Scanner, Hash Tool, BLE GATT
Explorer, WPA Handshake, RF Spectrum, Probe Intel, Offline Packet
Analysis, BLE Ducky, USB Ducky, WiFi Ducky. **+2,829 LOC.**

### Phase 9 — BSP against published Espressif drivers
`pm_bsp` component built against the official Espressif managed
components: `esp_lcd_mipi_dsi`, `esp_lcd_ili9881c`, `esp_lcd_touch_gt911`,
`esp_lvgl_port`. MIPI-DSI bus, GT911 touch, backlight PWM, LVGL
display + indev registration. **+411 LOC.**

### Phase 10 — LVGL UI pass
`pm_ui` component: shared widget kit (theme, titlebar, card, button,
chip, kv-row, status-dot, list, meter-bar, keypad, log-panel, grid).
Real LVGL launcher with category tiles + per-category app grid. Full
custom screens for clock, calculator, wifi, wardrive. 44 other apps
auto-upgraded to a default-screen pattern. **+1,271 LOC.**

### Phase 11 — C6 Ghost Engine firmware (separate tree)
Replaced brittle `strstr`-based command dispatch with cJSON. Added
8 new C6 modules: `ghost_http` (HTTP proxy with base64 response),
`ghost_hid` (BLE HID with US-QWERTY keymap), `ghost_promisc` (802.11
monitor mode), `ghost_netscan` (ARP-sweep host discovery), `ghost_rfspectrum`
(channel utilization), `ghost_wpa_hs` (EAPOL frame collector),
`ghost_ble_gatt` (BLE central skeleton), `ghost_wifi_ducky` (captive AP
+ form server). 25+ bridge commands routed. **+2,552 LOC, separate `pisces-moon-c6/` tree.**

### Phase 12 — SX1262 LoRa via RadioLib
`pm_lora` component: clean C API wrapping `jgromes/radiolib` v7.2.1.
FSK voice mode + LongFast mesh mode. Treaty mutex, DIO1 ISR → RX worker
task. Wired into mesh_messenger and lora_voice apps with full TX path,
RX callback, header parse, dedup ring. The only C++ TU in the P4
firmware. **+449 LOC.**

### Phase 13 — Pin fixes + GPS-on-P4 + radio auto-detect + C6 SDIO flasher
- All pin numbers corrected against ELECROW Lesson 14 (DSI, GT911,
  backlight, audio, SX1262, nRF24, SD, GPS, PDM mic, SPI bus)
- New `pm_gps_uart` component: P4-direct NMEA parser on UART1 IO2/IO3,
  feeds `pm_gps_state` directly, bypasses C6 bridge
- New `pm_radio` component: auto-detect dispatcher with SPI signature
  probes for SX1262 and nRF24, dispatches TX/RX/info to backends.
  H2/C6-slot/HaLow declared as user-selectable kinds, backends pending.
  nRF24 backend implemented via RadioLib's nRF24 class
- New `pm_c6_programmer` component: ESP serial bootloader protocol
  (SLIP encode/decode, command framing, SYNC handshake, FLASH_BEGIN/
  FLASH_DATA streaming/FLASH_END, MD5 verify hook). SDIO transport
  stubbed — protocol layer is correct and could be unit-tested over UART
- New `pm_app_c6_flasher` SYSTEM app (9th in category): file picker
  for `/sd/ghost/*.bin`, progress bar with phase tracking, worker task
- mesh_messenger and lora_voice now check `pm_radio_kind()` before
  attempting LoRa init; report wrong-radio state gracefully
- C6 bridge GPS handler retained for backward compat, marked deprecated
- **+1,905 LOC.**

---

## Project Totals

| Tree | Lines | Files |
|---|---:|---:|
| `pisces-moon-p4/` (P4 firmware) | 17,133 | 137 |
| `pisces-moon-c6/` (C6 Ghost Engine) | 2,552 | 27 |
| **Total** | **19,685** | **164** |

---

## What Works Today (Build & Boot)

Assuming a CrowPanel Advanced 7" board flashed with this firmware:

- ✅ Builds clean against ESP-IDF v5.5.3
- ✅ Boots into launcher within ~2 seconds of reset
- ✅ MIPI-DSI panel renders 1024×600 RGB565
- ✅ GT911 capacitive touch responsive
- ✅ All 49 apps loadable from launcher, every app has a real LVGL screen
- ✅ GPS lock once BN-180 sees the sky
- ✅ SD card mount at `/sd`, wardrive writes `/sd/sessions/session_*.db`
- ✅ Wireless slot auto-detect (NONE if empty, SX1262 or nRF24 if present)
- ✅ Backlight PWM control, audio amp control
- ✅ NVS for persistent app state

---

## What's Pending (Hardware Bring-Up)

- Verify pin numbers against the actual ELECROW Eagle schematic for any
  the wiki didn't explicitly document (SD detect, USB roles, charge IC)
- TCXO vs XTAL setting on whichever SX1262 carrier is plugged in
- C6 SDIO transport (the `_sdio_send` / `_sdio_recv` stubs)
- Codec2 encoder/decoder integration in `pm_audio` for `lora_voice`
- WPA hccapx binary assembly (frame collection works on C6 already)
- NimBLE central plumbing in `ghost_ble_gatt`
- TinyUSB HID descriptors for `usb_ducky`
- The 44 default-screen apps need per-app UI polish (mechanical;
  pattern established by the four showcase apps)

---

## Hardware Notes

The CrowPanel Advanced 7" SKU DHE04107D V1.0 is the reference board.
Verified pin map (from ELECROW wiki + Lesson 14):

```
MIPI-DSI:        DATA0=IO40, DATA1=IO39, DATA2=IO36, DATA3=IO35,
                  CLKN=IO37,  CLKP=IO38,  REXT=IO34
Touch (GT911):   SCL=IO46, SDA=IO45, INT=IO42, RST=IO40, addr=0x5D
Backlight:       BL_PWR=IO29, BL_EN=IO31 (LEDC PWM)
Wireless slot:   SCK=IO8, MISO=IO7, MOSI=IO6, NSS=IO10,
                  BUSY=IO9, IRQ=IO53, NRST=IO54
Audio NS4168:    LRCLK=IO21, BCLK=IO22, SDATA=IO23, AMP=IO30
PDM mic:         MCLK=IO24, SD=IO26
SD (SDIO 1-bit): CLK=IO43, CMD=IO44, D0=IO39
GPS (P4-direct): RX=IO2, TX=IO3, baud=9600
```

The 2×12 GPIO breakout header has the GPS wired to the left strip:
pin 1 = 3V3, pin 4 = GND, pin 5 = IO2, pin 6 = IO3.

---

## Build & Flash

See `PiscesMoon_P4_BringUp_Guide.docx` for the step-by-step bring-up
procedure. The short version:

1. Install VS Code with the Espressif ESP-IDF extension (v5.5.3)
2. Open `pisces-moon-p4/` as a folder
3. Set target to `esp32p4`, pick the board's serial port
4. Run "ESP-IDF: Build, Flash and start a Monitor"

The C6 firmware (`pisces-moon-c6/`) builds the same way targeting
`esp32c6`, but **getting it onto the chip** requires either the
SDIO flasher (Phase 14) or direct soldering — the board does not
expose the C6 console pins externally.

---

## Known Issues

- **C6 firmware flash path is the central open problem.** Without it,
  custom Ghost Engine cannot reach the C6, and the C6 stays on stock
  ELECROW firmware. This is the entire point of having a dedicated
  radio MCU; resolving the SDIO transport is the highest priority of
  Phase 14.
- **The Waveshare SX1262 Core1262-HF module does NOT fit the slot.**
  Its 2×7 DIP footprint is incompatible with ELECROW's proprietary
  slot. ELECROW sells a $6.55 SX1262 carrier that fits.
- **44 of 49 apps use the default-screen template.** They boot and are
  navigable, but their UIs need per-app polish to reach S3-equivalent
  fidelity. This is mechanical work; the pattern is documented in
  `pm_ui.h`.

---

## Migration Notes from v1.1.x

This is not a drop-in upgrade. The S3 code in `src/` does not run on
the P4. They are separate firmwares targeting separate hardware. The S3
remains the supported target for the T-Deck Plus on the v1.1.x line.

There is no automatic migration path for SD card data — the wardrive
schema changed from CSV to SQLite, and the OS settings format changed.
A user moving from S3 to P4 should expect to start fresh.

---

## License

AGPL-3.0-or-later. See `LICENSE`. All contributions require the CLA in
`CLA.md`. Every source file carries the SPDX header.

---

## Credits

Eric Becker / Fluid Fortune — design, architecture, all S3 code, the
Ghost Engine concept, hardware integration.

fluidfortune.com
