<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Pisces Moon OS

**A modular operating system for ESP32 hardware.**

Pisces Moon OS is open-source firmware (AGPL-3.0-or-later) that runs on a
host MCU, auto-detects whatever modules are attached, and exposes a
uniform capability surface to applications. Plug in an NFC reader, a
LoRa transceiver, a second radio coprocessor, a camera — apps see the
new capabilities appear; unplug them, apps see them disappear. The OS
never crashes because a module is missing.

This is the **Pisces Moon P4** branch (v1.2.0-alpha), the dual-MCU
implementation targeting boards with an ESP32-P4 application processor
and an ESP32-C6 always-on radio coprocessor.

**Status:** Alpha — full firmware tree builds, hardware bring-up
pending on bench.

**Repo:** github.com/FluidFortune/PiscesMoon
**Web:** [fluidfortune.com](https://fluidfortune.com)
**License:** AGPL-3.0-or-later
**Contributors:** see [CLA.md](CLA.md)

---

## Why modular

Every existing ESP32 OS we've seen ties itself to a single board. Plug
that exact board in, flash the firmware, it works. Plug in any
*variation* of that board — a different revision, a missing peripheral,
an added peripheral — and the firmware either silently breaks or
crashes on boot.

We watched this play out with Kode Dot. Their hardware is modular by
design; their software pretends it isn't. The expansion slots have no
published pinout because there's no software contract that says how a
module identifies itself, what it offers, or how an app should find it.

Pisces Moon takes the opposite stance.

**Apps don't ask "does this board have WiFi?" Apps ask: *"is a WiFi peer
available?"*** The peer registry answers. If something is plugged in, it
registers. If it's unplugged, it withdraws. Apps query by capability,
not by hardware.

This means the same firmware runs on:

- An ESP32-P4 board with one C6 and nothing else attached
- The same board with an NFC reader plugged into the C6's UART
- The same board with a T-Beam Supreme S3 wired to the GPIO header for
  secondary radios
- The same board with a wireless slot module installed
- All of the above simultaneously
- A future Kode Dot board with whatever modules they ultimately expose

No conditional compilation. No board profiles. No "if-CrowPanel" code.
The OS auto-detects, registers, and routes.

---

## Reference chassis: ELECROW CrowPanel Advanced 7"

The first supported board is the ELECROW CrowPanel Advanced 7" HMI
(SKU DHE04107D V1.0). It's a $46 ESP32-P4 development board with a
7" 1024×600 touchscreen, an onboard C6 coprocessor, and several
modular expansion points.

### Permanent fixtures (always present)

- **ESP32-P4** (dual-core RISC-V LX9 @ 360 MHz, 32 MB PSRAM, 16 MB flash)
- **ESP32-C6-MINI-1** coprocessor (WiFi 6 / BLE 5 / 802.15.4) — runs
  the always-on Ghost Engine radio firmware
- **Beitian BN-180 GPS** — wired P4-direct to UART1 on the 2×12 header
- **1024×600 MIPI-DSI panel** + GT911 capacitive touch
- **NS4168 stereo codec** + dual speakers + PDM microphone
- **MicroSD via SDIO**

### Modular peers (optional, hot-pluggable, auto-detected)

| Peer | Detection | Capabilities |
|---|---|---|
| Wireless slot module | SPI signature probe | SX1262 LoRa, nRF24 2.4 GHz, ESP32-H2 Thread/Zigbee, ESP32-C6 slot variant, Wi-Fi HaLow |
| PN532 NFC reader | C6 UART1 probe (HSU mode) | NFC read, write, emulate, MIFARE Classic, NTAG, Amiibo |
| T-Beam Supreme S3 | P4 UART2 ping-handshake (IO25/IO27) | Secondary WiFi, secondary BLE, primary LoRa (when slot empty) |
| CSI camera | MIPI-CSI bus probe | Viewfinder, snapshot, barcode/QR decode |
| BLE HID device | C6 BLE central pairing | Gamepad input, keyboard input |

---

## Routing rules

When multiple peers offer the same capability, the registry chooses
based on the **role** the caller requested.

```
WiFi scan / BLE scan:
    PRIMARY    → ESP32-C6 (always-on Ghost Engine, never stops)
    SECONDARY  → T-Beam Supreme S3 (if connected)
    ANY        → whichever is available

LoRa transmit / receive:
    PRIMARY    → wireless slot SX1262 (if installed)
    FALLBACK   → T-Beam Supreme S3 LoRa (if T-Beam connected, no slot)
    UNAVAILABLE if neither

NFC read / write / emulate:
    PRIMARY    → PN532 on C6 UART1 (if connected)
    UNAVAILABLE otherwise

GPS fix:
    PRIMARY    → BN-180 P4-direct (always — permanent fixture)

Camera (snapshot / barcode / viewfinder):
    PRIMARY    → CSI camera on CIS-CAM connector
    UNAVAILABLE otherwise

Input (gamepad / keyboard):
    PRIMARY    → paired BLE HID device via C6
    FALLBACK   → on-screen virtual gamepad (always available)
    FALLBACK   → on-screen QWERTY overlay (always available)
```

The **secondary** role enables parallel ops. While the C6 wardrives
continuously, an app can ask for `wifi_scan` with role `SECONDARY` and
get a T-Beam handle to scan in parallel — without interrupting the
canonical capture log.

---

## What's in the box (this branch)

This repository (`pisces-moon-p4/`) is the application processor
firmware. The companion C6 coprocessor firmware lives at
`pisces-moon-c6/` (separate ESP-IDF project, shipped in the same
release).

A third tree (`pisces-moon-tbeam-s3/`) for the optional T-Beam Supreme
S3 secondary radio peer is **defined but not yet implemented**; Phase
15 lands the P4-side protocol and stub, with the actual T-Beam firmware
deferred to Phase 16.

```
pisces-moon-p4/                P4 firmware (this repo)
├── main/                      app_main, init sequence, app registration
├── components/
│   ├── pm_hal/                low-level helpers (logging, SPI Treaty, NVS)
│   ├── pm_bsp/                MIPI-DSI panel + GT911 touch + LVGL plumbing
│   ├── pm_ui/                 widget kit + on-screen keyboard + virtual gamepad
│   ├── pm_input/              unified input dispatcher (touch + BT HID + virtual)
│   ├── pm_peer/               MODULAR PEER REGISTRY — the OS spine
│   ├── pm_app_iface/          pm_app_t struct, category enum
│   ├── pm_audio/              NS4168 codec + I2S + PDM mic
│   ├── pm_nosql/              key-value store on SD
│   ├── pm_sqlite/             wardrive session DB
│   ├── pm_gps_state/          shared GPS fix cache
│   ├── pm_gps_uart/           P4-direct NMEA parser (BN-180)
│   ├── pm_radio/              wireless slot abstraction + auto-detect
│   ├── pm_lora/               SX1262 backend (RadioLib wrapper, only C++ TU)
│   ├── pm_nfc/                PN532 via C6 bridge (Phase 15)
│   ├── pm_camera/             CSI camera component (Phase 15)
│   ├── pm_tbeam/              T-Beam Supreme S3 peer (Phase 15 skeleton)
│   ├── pm_c6_bridge/          UART bridge to the C6
│   ├── pm_c6_programmer/      C6 firmware flasher (future-board support)
│   └── pm_apps/               56 apps across 7 categories
│       ├── system/   (9)
│       ├── tools/    (6, +1 Phase 15: QR scanner)
│       ├── intel/    (7)
│       ├── games/    (7)
│       ├── media/    (3, +1 Phase 15: camera)
│       ├── comms/    (6)
│       └── cyber/    (19, +5 Phase 15: 4 NFC apps + secondary scan)
├── partitions.csv
├── sdkconfig.defaults
├── CMakeLists.txt
├── README.md                  this file
├── CHANGELOG_v1_2_0_alpha.md
├── CHANGELOG_v1_2_0_phase15.md
├── CLA.md
├── LICENSE
└── docs/
    ├── ARCHITECTURE.md
    ├── PiscesMoon_P4_BringUp_Guide.docx
    └── PiscesMoon_OS_Architecture_Reference.docx

pisces-moon-c6/                C6 Ghost Engine + sensor coprocessor firmware
├── main/
│   ├── ghost_main.c
│   ├── ghost_bridge.c         JSON command dispatch
│   ├── ghost_wifi.c           WiFi scanner + STA
│   ├── ghost_ble.c            BLE scanner
│   ├── ghost_promisc.c        802.11 monitor mode
│   ├── ghost_http.c           HTTP proxy
│   ├── ghost_hid.c            BLE HID peripheral (keyboard out)
│   ├── ghost_hid_host.c       BLE HID central (gamepad/keyboard in)
│   ├── ghost_ble_gatt.c       BLE GATT central
│   ├── ghost_netscan.c        ARP-sweep host discovery
│   ├── ghost_rfspectrum.c     channel utilization
│   ├── ghost_wpa_hs.c         EAPOL handshake collector
│   ├── ghost_wifi_ducky.c     captive AP + form server
│   ├── ghost_nfc.c            PN532 driver on UART1 (Phase 15)
│   └── ghost_gps.c            legacy GPS (deprecated; P4 reads direct)
└── ...
```

---

## Apps

56 apps across 7 categories. The Phase 15 additions are marked.

### SYSTEM (9)
About · Files · File Manager · ELF Browser · Gamepad pairing · Bridge
console · MicroPython · System info · **C6 Flasher**

### TOOLS (6)
Notepad · Calculator (with subnet calculator mode) · Clock + stopwatch
· Calendar · Etch (drawing) · **Camera QR scanner** *(Phase 15)*

### INTEL (7)
Terminal · Gemini Log · Ref Med · Ref Surv · Baseball Live (MLB) ·
Trails (hiking POI/heat-map) · SSH client

### GAMES (7)
Snake · Pac-Man · Galaga · Chess · Doom · SimCity · Retro ELF launcher

### MEDIA (3)
Audio Player (WAV/MP3) · Audio Recorder (PDM mic) · **Camera (viewfinder
+ snapshot)** *(Phase 15)*

### COMMS (6)
GPS · WiFi · Bluetooth · LoRa Voice (push-to-talk) · Mesh Messenger
(Meshtastic LongFast) · Voice Terminal

### CYBER (19)
Wardrive (SQLite session DB) · BT Radar · Packet Sniffer · Beacon
Spotter · Network Scanner · Hash Tool · BLE GATT Explorer · WPA
Handshake · RF Spectrum · Probe Intel · Offline Packet Analysis · BLE
Ducky · USB Ducky · WiFi Ducky · **NFC Reader** · **NFC Clone** · **NFC
Emulate** · **Amiibo** · **Secondary Scan** *(Phase 15: T-Beam-routed
WiFi/BLE scan that runs alongside C6 wardrive)*

---

## Build

ESP-IDF v5.4.x project. The bring-up guide
(`docs/PiscesMoon_P4_BringUp_Guide.docx`) covers toolchain install
step-by-step.

**Quick version (CLI):**
```bash
. $IDF_PATH/export.sh
cd pisces-moon-p4
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

**VS Code:**
1. Install the Espressif ESP-IDF extension
2. "ESP-IDF: Configure ESP-IDF Extension" → EXPRESS → v5.4.x
3. Open `pisces-moon-p4/` as a folder
4. Set target to `esp32p4`, pick serial port
5. "ESP-IDF: Build, Flash and start a Monitor"

First build pulls ~4 GB of toolchain + managed components (5–10 min).
Incremental builds run in ~30 seconds.

### C6 firmware

Build separately, targeting `esp32c6`:
```bash
cd pisces-moon-c6
idf.py set-target esp32c6
idf.py build
```

**Flashing it onto the C6** is done via the **external UART1 connector**
on the top-right of the CrowPanel board (labeled "UART1" on the silkscreen
— routes directly to the C6's console UART). Use esptool with a USB-to-TTL
adapter. The board does not need to be disassembled; soldering is not
required.

The `pm_c6_programmer` component implements an alternate SDIO-based
flasher path for **future boards** that don't expose the C6 console
externally. On the CrowPanel Advanced 7", UART1 + esptool is the
supported path.

---

## What works today

- ✅ Builds clean against ESP-IDF v5.4.x with managed components
- ✅ Boots into launcher within ~2 seconds
- ✅ All 56 apps registered and loadable
- ✅ MIPI-DSI panel + GT911 touch + backlight PWM
- ✅ GPS lock once BN-180 sees the sky
- ✅ Wardrive writes `/sd/sessions/session_*.db`
- ✅ Wireless slot auto-detect (NONE / SX1262 / nRF24)
- ✅ Modular peer registry with C6 + BN-180 always registered
- ✅ PN532 NFC detection over C6 bridge (when present)
- ✅ Camera component skeleton ready (esp_video integration ahead)
- ✅ T-Beam peer skeleton ready (T-Beam firmware ahead)
- ✅ On-screen keyboard + virtual gamepad widgets
- ✅ BLE HID host on C6 (gamepad pairing)

## What's pending

- Hardware bring-up validation on the actual bench
- T-Beam Supreme S3 peer firmware (Phase 16)
- Full `pm_camera` driver using `esp_video` managed component
- Several apps still on default-screen template (per-app UI polish)
- Codec2 voice encoder for `lora_voice`
- WPA hccapx binary assembly
- NimBLE GATT central plumbing finalization

---

## License

AGPL-3.0-or-later. See `LICENSE`.

In short: free to use, modify, and distribute. If you run a network
service based on this code, you must release your modified source.
Every source file carries an SPDX header.

Contributions require the CLA in `CLA.md`. By submitting a pull
request, you agree to license your contribution under the project's
license.

---

## Credits

**Eric Becker** / **Fluid Fortune** — design, architecture, modular
peer system, hardware integration, all of the above.

Built on the work of the Espressif ESP-IDF team, the LVGL project,
RadioLib (Jan Gromeš), siara-cc/sqlite3, and the broader ESP32
community. Hardware reference: the ELECROW CrowPanel Advanced 7" team.

[fluidfortune.com](https://fluidfortune.com)
