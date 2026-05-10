<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Pisces Moon OS — v1.2.0-alpha "Pisces Moon P4"

A general-purpose, hackable operating system for the **ELECROW CrowPanel
Advanced 7" ESP32-P4 HMI** (SKU DHE04107D), with a dedicated **ESP32-C6**
coprocessor running an always-on radio capture daemon ("Ghost Engine").

**Repo:** github.com/FluidFortune/PiscesMoon
**Web:** fluidfortune.com
**License:** AGPL-3.0-or-later
**Status:** Alpha (full firmware tree built, hardware bring-up pending)

---

## What is this?

Pisces Moon is a 49-app desktop-style OS that runs on a 7-inch touchscreen
device about the size of a paperback book. It does:

- **Tools** — clock, calculator (with subnet calculator mode), notepad, calendar, drawing
- **Intel** — terminal, AI chat (Gemini), reference docs, baseball scores, hiking trails, SSH client
- **Games** — Snake, Pac-Man, Galaga, Chess, Doom, SimCity, Retro ELF launcher
- **Media** — audio player and recorder
- **Comms** — GPS, WiFi, Bluetooth, LoRa voice, mesh messenger (Meshtastic LongFast), voice terminal
- **Cyber** — wardriving with SQLite session DB, Bluetooth radar, packet sniffer, beacon spotter, network scanner, hash tool, BLE GATT explorer, WPA handshake collector, RF spectrum analyzer, probe intelligence, offline packet analysis, BLE/USB/WiFi Ducky
- **System** — about, files, file manager, ELF browser, gamepad pairing, Bridge console, MicroPython, **C6 Ghost firmware flasher**

The whole thing is open-source under AGPL-3.0-or-later. Every header
file has a copyright stamp and SPDX identifier.

---

## The architectural distinction

The killer-feature framing is **Ghost Engine on its own MCU**.

The CrowPanel Advanced 7" has two ESP32 chips on one board: a powerful
**ESP32-P4** for the UI and applications, and a small **ESP32-C6**
dedicated to wireless. Pisces Moon uses this split deliberately:

- The P4 runs the UI, apps, GPS, audio, LoRa, and SD storage
- The C6 runs custom firmware that wardrives **continuously** —
  scanning WiFi and BLE, decoding packets, collecting EAPOL handshakes —
  regardless of what the P4 is doing
- The two chips talk over UART using a JSON-framed bridge protocol with
  25+ commands

On the older S3 platform the Ghost Engine had to share cycles with games
and the UI. On the P4/C6 platform, it doesn't. Open Doom; the C6 is still
wardriving. Reboot the P4; the C6 keeps going.

This is the project's primary distinctive claim and a major part of why
the AGPL matters: an always-on, dedicated, non-stoppable radio MCU is a
specific architectural choice, and the AGPL ensures derivative work on
this stack stays open.

---

## Specs

| | |
|---|---|
| **Board** | ELECROW CrowPanel Advanced 7" ESP32-P4 HMI (DHE04107D V1.0) |
| **Main MCU** | ESP32-P4, dual-core RISC-V LX9 @ 360 MHz |
| **Coprocessor** | ESP32-C6-MINI-1 (WiFi 6 / BLE 5 / 802.15.4) |
| **RAM** | 32 MB PSRAM |
| **Flash** | 16 MB |
| **Display** | 7" IPS 1024×600, MIPI-DSI, ILI9881C panel |
| **Touch** | GT911 capacitive |
| **Audio** | NS4168 codec, dual speakers, PDM microphone |
| **Storage** | MicroSD via SDIO 1-bit |
| **GPS** | Beitian BN-180 on UART1 |
| **Wireless slot** | SX1262 / nRF24 / ESP32-H2 / ESP32-C6 / Wi-Fi HaLow (auto-detect) |
| **Camera** | MIPI-CSI port (no app yet) |
| **Power** | USB-C, lithium battery JST |
| **Buttons** | RESET, BOOT |

---

## Build

This is an ESP-IDF v5.4 project. **You need ESP-IDF installed** —
either via the official Espressif VS Code extension (recommended) or
from the command line.

The bring-up guide (`docs/PiscesMoon_P4_BringUp_Guide.docx`) walks
through this in detail. Short version:

```bash
. $IDF_PATH/export.sh
cd pisces-moon-p4
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

For VS Code users:
1. Install the Espressif ESP-IDF extension
2. Run "ESP-IDF: Configure ESP-IDF Extension" — pick EXPRESS and v5.4.x
3. File → Open Folder → select `pisces-moon-p4/`
4. Bottom toolbar: set target to `esp32p4`, pick serial port
5. Run "ESP-IDF: Build, Flash and start a Monitor"

The first build pulls ~4 GB of toolchain and managed components and
takes 5–10 minutes. After that, incremental builds are ~30 seconds.

### C6 firmware

The C6 firmware in `pisces-moon-c6/` builds the same way targeting
`esp32c6`. **Getting it onto the chip** is harder — see "C6 firmware
flash" below.

---

## Repository layout

```
pisces-moon-p4/
├── main/                             P4 firmware entry point
│   ├── main.c                        app_main(), init sequence
│   ├── pm_launcher.c                 LVGL launcher
│   ├── pm_apps_register.c            registers all 49 apps
│   ├── CMakeLists.txt
│   ├── idf_component.yml             managed components (LVGL, RadioLib, ...)
│   └── ...
├── components/                       OS subsystems, each its own ESP-IDF component
│   ├── pm_hal/                       low-level HAL (SPI Treaty, time, NVS, CRC, log)
│   ├── pm_bsp/                       MIPI-DSI + GT911 + LVGL plumbing
│   ├── pm_ui/                        Pisces UI kit (theme + widgets)
│   ├── pm_app_iface/                 pm_app_t struct, category enum
│   ├── pm_audio/                     NS4168 + I2S + PDM mic
│   ├── pm_nosql/                     simple KV store on SD
│   ├── pm_sqlite/                    wardrive session DB
│   ├── pm_gps_state/                 shared GPS cache
│   ├── pm_gps_uart/                  P4-direct NMEA parser
│   ├── pm_radio/                     wireless slot abstraction + auto-detect
│   ├── pm_lora/                      SX1262 backend (RadioLib wrapper)
│   ├── pm_c6_bridge/                 UART bridge to the C6
│   ├── pm_c6_programmer/             C6 firmware flasher (SDIO bootloader)
│   └── pm_apps/                      All 49 apps, grouped by category
│       ├── system/   (9 apps)
│       ├── tools/    (5 apps)
│       ├── intel/    (7 apps)
│       ├── games/    (7 apps)
│       ├── media/    (2 apps)
│       ├── comms/    (6 apps)
│       └── cyber/    (14 apps)
├── partitions.csv
├── sdkconfig.defaults
├── CMakeLists.txt
├── README.md                         (this file)
├── CHANGELOG_v1_2_0_alpha.md         this release
├── CLA.md                            contributor license agreement
├── LICENSE                           AGPL-3.0-or-later (full text)
└── docs/
    ├── PiscesMoon_P4_BringUp_Guide.docx
    └── PiscesMoon_OS_Architecture_Reference.docx

pisces-moon-c6/                       C6 Ghost Engine firmware (separate project)
├── main/
│   ├── ghost_main.c
│   ├── ghost_bridge.c                cJSON command dispatcher
│   ├── ghost_wifi.c                  WiFi scanner + STA control
│   ├── ghost_ble.c                   BLE scanner
│   ├── ghost_promisc.c               802.11 monitor mode
│   ├── ghost_http.c                  HTTP proxy with base64 response
│   ├── ghost_hid.c                   BLE HID keyboard
│   ├── ghost_ble_gatt.c              BLE GATT central
│   ├── ghost_netscan.c               ARP-sweep host discovery
│   ├── ghost_rfspectrum.c            channel utilization sweep
│   ├── ghost_wpa_hs.c                EAPOL handshake collector
│   ├── ghost_wifi_ducky.c            captive AP + form server
│   ├── ghost_gps.c                   GPS reader (legacy, see note)
│   └── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
└── CMakeLists.txt
```

---

## What works today

- ✅ Builds clean against ESP-IDF v5.4.x with managed components
- ✅ Boots into launcher within ~2 seconds of reset
- ✅ MIPI-DSI panel, GT911 touch, backlight all controlled
- ✅ All 49 apps loadable, real LVGL screens, navigable
- ✅ GPS lock once BN-180 sees the sky
- ✅ SD wardrive writes `/sd/sessions/session_*.db`
- ✅ Wireless slot auto-detect (NONE if empty, SX1262 or nRF24 if present)
- ✅ Audio amp control (Codec2 voice path pending)

---

## What's pending

- C6 SDIO flasher transport (protocol layer is complete, transport stubbed)
- Codec2 encoder/decoder for `lora_voice`
- WPA hccapx binary assembly (frames are collected; binary format needs writing)
- NimBLE GATT central plumbing in `ghost_ble_gatt`
- TinyUSB HID descriptors in `usb_ducky`
- 44 of 49 apps use a default-screen template (functional; per-app polish ahead)
- Hardware bring-up validation against the actual board

See `CHANGELOG_v1_2_0_alpha.md` for full status.

---

## C6 firmware flash

Custom Ghost Engine firmware on the C6 is the project's central
distinguishing feature, and **getting it onto the chip is the hardest
remaining problem**.

The board does not expose the C6 console pins externally, so direct
USB-to-serial flashing isn't an option without soldering. The path
forward is **P4-mediated SDIO flashing**: the P4 acts as the programmer,
drives the C6's BOOT/EN pins via SDIO sideband, and pushes firmware
blocks using the ESP serial bootloader protocol tunneled through SDIO.

The protocol layer is implemented in `pm_c6_programmer.c` (SLIP framing,
command structure, SYNC handshake, FLASH_BEGIN/DATA/END streaming). The
SDIO transport itself (`_sdio_send` / `_sdio_recv`) is currently stubbed
and is the Phase 14 priority.

A C6 Flasher app exists in the SYSTEM category and provides the UI
(file picker for `/sd/ghost/*.bin`, progress bar, phase tracking).
Once the transport is brought up, flashing the C6 will be a button tap.

---

## Hardware notes

This firmware targets the CrowPanel Advanced 7" SKU DHE04107D V1.0
specifically. Pin numbers are verified from the ELECROW wiki and
Lesson 14 — see the changelog for the full pin map.

GPS wiring: the BN-180 connects to the 2×12 GPIO breakout header on
the left strip — pin 1 = 3V3, pin 4 = GND, pin 5 = IO2 (GPS TX → P4 RX),
pin 6 = IO3 (P4 TX → GPS RX, mostly unused).

The wireless module slot accepts ELECROW's interchangeable carriers:
SX1262 (LoRa, $6.55), nRF24 ($3.50), ESP32-H2, ESP32-C6 slot variant,
or Wi-Fi HaLow. SX1262 and nRF24 auto-detect via SPI signature probe
at boot.

The Waveshare Core1262-HF module does **not** fit the slot — buy
ELECROW's own SX1262 carrier instead.

---

## Contributing

Pull requests welcome. By submitting, you agree to the terms in
`CLA.md`. The project is AGPL-3.0-or-later; if you fork and run a
service from the fork, the AGPL requires you to publish the source.

If you're new to embedded development and want to help, the most
accessible work is **per-app UI polish**: 44 of 49 apps currently use
the default-screen template and would benefit from custom LVGL
screens. The pattern is documented in `pm_ui.h` and four reference
apps (clock, calculator, wifi, wardrive) demonstrate the kit.

If you have hardware experience, the highest-impact pending work is
the C6 SDIO transport in `components/pm_c6_programmer/`.

---

## License

AGPL-3.0-or-later. See `LICENSE` for the full text.

In short: free to use, modify, and distribute. If you run a network
service based on this code, you must release your modified source.
Every source file carries an SPDX header identifying the license.

---

## Credits

**Eric Becker** / **Fluid Fortune** — design, architecture, S3 lineage,
Ghost Engine concept, hardware integration.

Built on the work of the Espressif ESP-IDF team, the LVGL project, the
RadioLib project (Jan Gromeš), the SQLite project (siara-cc port), and
the broader ESP32 community.

fluidfortune.com
