<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Pisces Moon OS — Project Bible

**Document role:** Canonical reference for everyone — Eric, Claude
instances (Desktop and Code), Codex, future contributors.

**Reading order:** Start at the top. Every section is intentional. If
you're handing off to a new AI instance, paste this document first;
then paste the latest phase changelog; then begin.

**Last revised:** 2026-05-28 (Phase 16 "Live Iron" — hardware-validated)
**Version:** v1.2.0-alpha
**Repo:** github.com/FluidFortune/PiscesMoon
**Web:** fluidfortune.com
**License:** AGPL-3.0-or-later

---

## Table of Contents

1. [What Pisces Moon Is](#1-what-pisces-moon-is)
2. [Why Modular](#2-why-modular)
3. [Device Family](#3-device-family)
4. [Reference Hardware (P4)](#4-reference-hardware-p4)
5. [Core Architecture](#5-core-architecture)
6. [The Multi-Device Architecture (Cardputer Bridge)](#6-the-multi-device-architecture-cardputer-bridge)
7. [Modular Peer Registry](#7-modular-peer-registry)
8. [Routing Rules](#8-routing-rules)
9. [Storage Tiering](#9-storage-tiering)
10. [Always-On Device Identity](#10-always-on-device-identity)
11. [Component Layout](#11-component-layout)
12. [App System](#12-app-system)
13. [UI Layer](#13-ui-layer)
14. [Build & Flash](#14-build--flash)
15. [Phase History](#15-phase-history)
16. [Roadmap](#16-roadmap)
17. [The Bible's Locked Conventions](#17-the-bibles-locked-conventions)
18. [The Multi-Agent Pipeline](#18-the-multi-agent-pipeline)
19. [Open Work and Known Issues](#19-open-work-and-known-issues)
20. [Glossary](#20-glossary)

---

## 1. What Pisces Moon Is

Pisces Moon OS is open-source firmware (AGPL-3.0-or-later) for ESP32-class
hardware. It is **not** an RTOS extension, not an Arduino sketch
collection, and not a single-board firmware blob. It is an actual
operating system: a kernel-like host that boots, owns the hardware,
exposes capabilities through a registry, and runs apps as guests.

The defining feature is that **apps don't talk to hardware**. Apps talk
to **peers**. A peer is whatever provides a capability — could be the
onboard radio, could be a coprocessor on the SDIO bus, could be a USB
HID device, could be a sister ESP32 board over UART. Apps ask the peer
registry "I need a LoRa transmitter" and the OS routes the call to
whichever peer can satisfy it.

This sounds like vaporware. It is not. As of Phase 16, the system
boots on real hardware, the launcher loads, apps launch, the M5
Cardputer ADV connected over UART1 acts as a live radio/input/GPS
peer, and the Wardrive app runs WiFi scans through the onboard C6
*and* BLE scans through the Cardputer *simultaneously* — all routed
through the same uniform peer interface that any app uses.

### What Pisces Moon explicitly is not

- **Not an IoT logger.** Not sleep-targeting, not power-optimized for
  battery life over function, not a sensor uploader.
- **Not a single-app firmware.** A specific app does not own the
  device. The OS owns the device. Apps are guests.
- **Not portable in the "runs on any ESP32" sense.** It targets specific
  hardware families with curated profiles. Trying to make it run on
  arbitrary ESP32 boards would compromise the modular-peer abstraction.
- **Not a toolkit/library.** It is intended to be flashed and run.

### What Pisces Moon explicitly is

- **A portable computing device OS** — always-on during use, display
  stays on, full UI, multi-app, multi-radio.
- **A modular hardware coordinator** — plug something in, the OS
  detects it and apps see new capabilities; unplug it, apps see them
  withdraw. No reboot needed for capability advertisement; reboot
  required for hot-plug re-detection.
- **A multi-edition product family** — same kernel, curated app sets
  per market (Cyber Edition, Audio Edition, Field/Reference Edition,
  Developer Edition).

---

## 2. Why Modular

Every existing ESP32 OS we've seen ties itself to one specific board.
That board, those exact peripherals, that exact pinout. Plug it in,
flash it, it works. Plug in any variation and the firmware either
silently breaks or crashes.

This is unacceptable for a real OS.

Pisces Moon was designed around watching Kode Dot fail at modularity.
Their hardware is modular by design (expansion slots, swappable
modules). Their software pretends it isn't. The expansion slots have
no published pinout because there's no software contract that says how
a module identifies itself, what it offers, or how an app should find
it.

The Pisces Moon stance: **apps don't ask "does this board have WiFi?"
Apps ask "is a WiFi peer available?"** The peer registry answers.
If something is plugged in, it registers. If it's unplugged, it
withdraws. Apps query by capability, not by hardware.

Concretely:

```c
// WRONG — what most ESP32 firmware does
if (BOARD_HAS_LORA) {
    sx1262_send(buf, len);
}

// RIGHT — what Pisces Moon does
pm_peer_t* p = pm_peer_find("lora_tx", PM_PEER_ROLE_PRIMARY);
if (p) {
    pm_peer_call(p, "lora_tx", buf, len);
}
```

Same firmware binary runs on:
- A P4 with one C6 and nothing else
- The same board with NFC plugged into the C6's UART
- The same board with a T-Beam Supreme S3 wired to the GPIO header
- The same board with a wireless slot module installed
- All of the above simultaneously
- A future Kode Dot board, whatever modules they expose

No conditional compilation. No board profiles. No "if-CrowPanel" code.
The OS auto-detects, registers, and routes.

---

## 3. Device Family

Pisces Moon now runs on **four hardware variants** in active
development. Each shares the same OS kernel and app-iface; UI and
peripheral mix differs.

### Pisces Moon P4 (this repo — primary development)
- **Hardware:** ELECROW CrowPanel Advanced 7" ESP32-P4 HMI
- **Display:** 1024×600 MIPI-DSI, GT911 capacitive touch
- **Coprocessor:** ESP32-C6 (onboard, ESP-Hosted SDIO) = Ghost Engine
- **Status:** Phase 16 — boot+launcher+apps confirmed on real silicon
- **Variant:** 5-inch (800×480) profile also builds clean

### Pisces Moon Cardputer ADV
- **Hardware:** M5Stack Cardputer ADV (ESP32-S3, full QWERTY, GPS, LoRa)
- **Display:** 240×135 LCD
- **Status:** v1.2.0 splash + launcher running; serves as radio/input
  bridge to P4 over UART1 (this is the multi-device architecture)

### Pisces Moon T-Deck Plus
- **Hardware:** LilyGo T-Deck Plus (ESP32-S3, trackball, full QWERTY)
- **Display:** 2.8" 320×240
- **Status:** Wardrive workhorse, real GPS lock confirmed
- **Role:** Field test platform for radio apps

### Pisces Moon T-LoRa Pager
- **Hardware:** LilyGo T-LoRa Pager (ESP32-S3, LoRa-native, GPS)
- **Display:** Small (160×80 or similar)
- **Status:** Boot screen functional, work in progress

### Hardware bridge relationships
Phase 16 introduced the **Cardputer ADV as a UART1 bridge to the P4**.
This isn't a hack — it's the canonical demonstration of the modular-peer
architecture. The Cardputer is just another peer. It announces its
capabilities (keyboard, GPS, BLE, LoRa, etc.) over UART using a
documented line protocol; the P4 registers it in `pm_peer`; apps query
the registry and route through it like any other peer.

The same protocol can be implemented by any future hardware
attachment. The T-Beam Supreme S3 will use a similar approach when
its firmware lands (Phase 17+).

---

## 4. Reference Hardware (P4)

The CrowPanel Advanced 7" is the first supported board and the
canonical reference platform.

### Permanent fixtures (always present)
- **ESP32-P4** application processor:
  - Dual-core RISC-V LX9 @ 360 MHz (HP cores)
  - Single LP core @ 40 MHz
  - 32 MB PSRAM
  - 16 MB flash
- **ESP32-C6-MINI-1** coprocessor, SDIO bus:
  - WiFi 6, BLE 5, 802.15.4 (Thread/Zigbee)
  - Runs Ghost Engine firmware (separate ESP-IDF project,
    `pisces-moon-c6/`)
  - Always-on radio routing for the P4
- **Beitian BN-180 GPS** — wired P4-direct to UART1 on the 2×12
  header (note: standalone usage parked Phase 16; GPS now comes from
  the Cardputer over UART1 bridge — see §6)
- **1024×600 MIPI-DSI panel** with EK79007 driver
- **GT911 capacitive touch controller**
- **NS4168 stereo I²S codec** + dual speakers + PDM microphone
- **MicroSD via SDIO** (10 MHz)

### Modular peers (optional, hot-pluggable, auto-detected)

| Peer | Detection method | Capabilities |
|---|---|---|
| Wireless slot module | SPI signature probe at boot | SX1262 LoRa, nRF24, ESP32-H2, ESP32-C6 plug-in, Wi-Fi HaLow |
| PN532 NFC reader | C6 UART1 probe (HSU mode) | NFC read, write, emulate, MIFARE Classic, NTAG, Amiibo |
| T-Beam Supreme S3 | P4 UART2 ping-handshake (IO25/IO27) | Secondary WiFi, secondary BLE, primary LoRa fallback |
| **M5 Cardputer ADV** | P4 UART1 (GPIO47/48 @ 921600) | Keyboard, GPS, BLE, LoRa, WiFi promiscuous |
| CSI camera | MIPI-CSI bus probe | Viewfinder, snapshot, barcode/QR decode |
| BLE HID device | C6 BLE central pairing | Gamepad input, keyboard input |

### Critical pinout (P4 side, current Phase 16)
- **UART1** = Cardputer bridge: RX=GPIO48, TX=GPIO47, 921600 baud
- **UART2** = T-Beam Supreme S3 ping-handshake (when present)
- **SDIO** = C6 coprocessor (ESP-Hosted)
- **MIPI-DSI** = display
- **MIPI-CSI** = camera (when present)
- **I²C** = touch (GT911), codec config
- **I²S** = audio codec
- **SDMMC** = MicroSD

### Incoming hardware (ordered, not yet on bench)
- SX1262 LoRa wireless slot module
- ESP32-H2 wireless slot module
- ESP32-C6 plug-in (for custom firmware development)
- nRF24L01 wireless slot module

When modules arrive, `pm_radio_init_auto()` detects them at boot. LoRa
mesh and promiscuous capture will no longer depend on the Cardputer
fallback path.

---

## 5. Core Architecture

### Cores

Five compute cores are available across the two chips. Each has a
specific role.

| Designation | Physical core | Role |
|---|---|---|
| **Ghost Engine (Core 0)** | ESP32-C6 | Always-on radio coprocessor. Runs WiFi station, BLE scan, ESP-Hosted SDIO transport. Separate firmware. |
| **OS Core** | P4 HP Core 0 (RISC-V @ 360 MHz) | Pisces Moon kernel, peer registry, app lifecycle, background workers (e.g. Wardrive logging). |
| **UI Core** | P4 HP Core 1 (RISC-V @ 360 MHz) | LVGL is pinned here. All UI rendering. Never blocks the OS Core. |
| **Sentinel Core** | P4 LP Core (RISC-V @ 40 MHz) | Reserved for Software PMU (Phase 17). Currently idle. |

**Note:** The C6 is "Core 0" by external naming convention but is
physically a separate chip. It does not share a scheduler with the P4
HP/LP cores. Treat it as a peer that happens to live on-board.

### Boot sequence (Phase 16, validated on hardware)

1. ESP-IDF startup (RISC-V boot ROM → second-stage bootloader → app_main).
2. `pm_hal_init()` — clock config, NVS, logging.
3. `pm_bsp_init()` — MIPI-DSI panel up, GT911 touch up, LVGL init,
   backlight ramp.
4. **`pm_boot_post()`** — POST-style boot status screen rendered.
5. **`pm_boot_splash()`** — cyberpunk splash with chip icon, "PISCES MOON
   / the OS" title, taglines, watermark.
6. `pm_audio_init()`, `pm_input_init()`, `pm_radio_init_auto()`,
   `pm_peer_init()`.
7. `pm_cardputer_i2c_init()` — UART1 bridge to Cardputer (registers
   itself in `pm_peer` once `HELLO` is received).
8. `pm_tbeam_init()` — one-shot probe; registers in `pm_peer` if
   present, fully shuts down its UART driver if absent.
9. `pm_apps_register_all()` — every app registered (lazy init).
10. `pm_launcher_start()` — category grid drawn, UI ready for input.

Total cold boot: ~2 seconds to launcher. (Splash adds ~1.5s of intentional
delay for visual polish.)

### App lifecycle (locked convention)

Every app implements `pm_app_t`:

```c
typedef struct {
    const char*  id;              // "wardrive", "mesh_messenger", etc.
    const char*  display_name;    // shown in launcher tile
    pm_cat_t     category;        // PM_CAT_CYBER, PM_CAT_COMMS, etc.
    uint16_t     icon_id;         // future: icon atlas index
    void (*init)   (void);        // called once at registration
    void (*enter)  (void);        // called every time app is opened
    void (*tick)   (uint32_t ms); // called from UI core ~10x/sec
    void (*exit)   (void);        // called when leaving (back button)
    void (*deinit) (void);        // called at OS shutdown (rare)
} pm_app_t;
```

**Locked rule:** Apps lazy-build their UI on first `_enter()`, never at
`init`. Pre-Phase-16 apps that built UI in `_init` caused boot delays
and competed for LVGL bandwidth. The lazy pattern is non-negotiable.

```c
static lv_obj_t* s_screen = NULL;

static void _enter(void) {
    if (!s_screen) {
        _build_screen();           // first enter only
    }
    if (s_screen) lv_screen_load(s_screen);
    pm_log_i(TAG, "enter");
    // ...refresh data, subscribe inputs, etc.
}
```

---

## 6. The Multi-Device Architecture (Cardputer Bridge)

**This is the architectural breakthrough of Phase 16.**

The M5 Cardputer ADV connects to the P4 via a physical UART1 header.
It announces its capabilities at boot and the P4 registers it as a peer.
From that point forward, the Cardputer is indistinguishable to apps
from any other peer — they query "give me a keyboard peer" or "give me
a GPS peer" and the registry routes the call.

### Wiring

| Signal | P4 GPIO | Cardputer |
|---|---|---|
| RX1 | GPIO48 | TX |
| TX1 | GPIO47 | RX |
| Baud | 921600 | 921600 |
| Ground | shared | shared |

### Line protocol

Lines the **Cardputer → P4**:

```
PMU1 HELLO caps=0xNNNN ver=...
PMU1 KEY code=N mod=N
PMU1 GPS lat=... lon=... alt=... fix=...
PMU1 BLE mac=XX:XX:XX:XX:XX:XX rssi=-NN name='...'
PMU1 LORA rssi=-NN snr_x4=N freq=906875 data=HEX
PMU1 WF type=N ch=N rssi=-NN mac=AABBCCDDEEFF len=N data=HEX
```

Commands the **P4 → Cardputer**:

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

### Confirmed working (Phase 16)
- `HELLO` — Cardputer announces itself and capabilities
- `KEY` — typing on the Cardputer drives input across all P4 apps
- `GPS` — Cardputer's BN-180 provides valid GPS fixes to the P4
- `BLE` — Cardputer scans BLE in parallel with C6 WiFi scanning

### Pending Cardputer firmware (Phase 17 work)
- `LORA` mesh commands and TX/RX
- `WF` WiFi promiscuous frame forwarding

### Cardputer peer capabilities (registered in `pm_peer`)
```
gps_remote, gps_status, lora_tx, lora_rx, lora_mesh,
wifi_scan, wifi_capture, wifi_promisc,
ble_scan, ble_gatt, keyboard_hid, s3_module
```

### Component implementation
`components/pm_cardputer_i2c/` — despite the legacy directory name (a
relic of an earlier I²C transport experiment), the active transport is
UART. Renaming the directory is a cosmetic Phase 17 item; the protocol
and implementation are stable.

### Why this matters
This proves the modular-peer architecture works. A completely separate
ESP32 board, communicating only by UART, is treated as a first-class
peer alongside the onboard C6. No app code knows or cares that GPS
comes from the Cardputer instead of the BN-180. **That** is the
abstraction working.

---

## 7. Modular Peer Registry

`components/pm_peer/` is the OS spine.

### Concept

A peer is anything that can offer a capability. Peers register
themselves with the registry; apps query the registry by capability
name and role.

### Capability names (string keys)

Stable conventions:
- `wifi_scan`, `wifi_capture`, `wifi_promisc`, `wifi_sta`, `wifi_ap`
- `ble_scan`, `ble_gatt`, `ble_hid_host`, `ble_hid_peripheral`
- `lora_tx`, `lora_rx`, `lora_mesh`, `lora_voice`
- `gps_status`, `gps_remote`, `gps_raw`
- `nfc_read`, `nfc_write`, `nfc_emulate`
- `keyboard_hid`, `gamepad_hid`
- `camera_viewfinder`, `camera_snapshot`, `camera_barcode`
- `audio_play`, `audio_record`
- `sd_storage`
- `s3_module` (Cardputer marker)

### Roles

```c
typedef enum {
    PM_PEER_ROLE_PRIMARY,    // first-choice provider
    PM_PEER_ROLE_SECONDARY,  // parallel/alternate
    PM_PEER_ROLE_FALLBACK,   // last resort
    PM_PEER_ROLE_ANY,        // any matching peer
} pm_peer_role_t;
```

### API surface

```c
pm_peer_t*   pm_peer_find    (const char* cap, pm_peer_role_t role);
const char*  pm_peer_name    (const pm_peer_t* p);
pm_peer_kind_t pm_peer_kind  (const pm_peer_t* p);
int          pm_peer_call    (pm_peer_t* p, const char* op, const char* args);
bool         pm_peer_acquire (pm_peer_t* p, const char* owner);
void         pm_peer_release (pm_peer_t* p);
```

### Concurrency

A peer can be "held" exclusively by one app (e.g. LoRa Voice acquires
the SX1262 for the duration of a transmission). Other apps requesting
the same peer either block, get a SECONDARY peer, or get an error
depending on the call.

Wardrive intentionally **does not acquire exclusively** — it shares.
That's why Mesh Messenger could fall back to the Cardputer for LoRa
even while Wardrive was scanning BLE on the same Cardputer link.

---

## 8. Routing Rules

When multiple peers can satisfy a capability, the registry chooses by
role and current device topology.

```
WiFi scan / BLE scan:
    PRIMARY    → ESP32-C6 (Ghost Engine, always-on)
    SECONDARY  → Cardputer ADV (if connected)
                 or T-Beam Supreme S3 (if connected)
    ANY        → first available

LoRa transmit / receive / mesh:
    PRIMARY    → wireless slot SX1262 (if installed)
    FALLBACK   → Cardputer ADV LoRa (if connected, no slot)
    FALLBACK   → T-Beam Supreme S3 LoRa (if connected, no slot)
    UNAVAILABLE if none

NFC read / write / emulate:
    PRIMARY    → PN532 on C6 UART1 (if connected)
    UNAVAILABLE otherwise

GPS fix:
    PRIMARY    → Cardputer ADV GPS (currently)
    [parked]   → P4-direct BN-180 (Phase 16 disabled, returns)
    UNAVAILABLE if no Cardputer

Camera:
    PRIMARY    → CSI camera on CIS-CAM connector
    UNAVAILABLE otherwise

Input (keyboard / gamepad):
    PRIMARY    → Cardputer ADV (when connected)
    PRIMARY    → paired BLE HID device via C6 (when present)
    FALLBACK   → on-screen virtual gamepad / QWERTY (always available)
```

### The Secondary trick

The SECONDARY role exists so apps can run **alongside** a primary user
without interrupting it. While the C6 wardrives continuously, an app
can ask for `wifi_scan` with role SECONDARY and get a Cardputer or
T-Beam handle to scan in parallel — without interrupting the canonical
capture log. This is how multi-radio parallelism works in Pisces Moon.

---

## 9. Storage Tiering

**Locked rule:** Different data shapes use different storage.

### Tier 1: Firehose logging (CSV/JSONL append-only files)

For high-volume capture where ingestion speed matters and queries
happen offline.

- Wardrive (WiFi/BLE captures, ~10–100/sec)
- Packet sniffer
- Beacon spotter
- BLE scanner

CSV append-only is the **default and hot path**. Per-record overhead is
single-digit milliseconds. Files exportable directly to wireshark,
spreadsheets, etc.

### Tier 2: Queryable user data (SQLite)

For data that needs structured queries, updates, joins.

- Notepad (settings, recent files)
- Calendar
- Contacts
- Library indexes
- User preferences

SQLite makes sense here because of the query capabilities.

### Tier 3: Schemaless KV (NoSQL)

`pm_nosql` — a tagged blob store on SD with a key per file.

- Reference apps (medical, survival, trails, baseball)
- Saved Gemini chat sessions
- Documents that are read whole or not at all

### Why not SQLite-everywhere

Wardrive previously used SQLite for every captured network/BLE/probe
record. Per-INSERT overhead at 10 MHz SDMMC was 100–500 ms — totally
unworkable for a firehose. SQLite remains available opt-in but is not
the default.

---

## 10. Always-On Device Identity

Pisces Moon is **a portable computing device**, not an IoT logger.

Concretely:
- Display stays on during active use.
- The OS does not sleep-target. There is no "low-power mode" by default.
- Battery target: 4–8 hours active use, not 30 days standby.
- The future Software PMU on the LP core (Sentinel Core, Phase 17) is
  a **reliability layer** for power-transition stability, not a
  battery-life optimizer.
- One wireless module at a time — the slot is exclusive per session.

Why this matters: it shapes every architecture decision. We don't
"wake up to sample" — we run continuously. We don't "batch and
upload" — apps stream live. We don't "deep sleep" — we keep state.

---

## 11. Component Layout

```
pisces-moon-p4/
├── main/
│   ├── main.c                 app_main, init sequence
│   ├── pm_app.c               app registry runtime
│   ├── pm_apps_register.c     calls every pm_app_*() to register
│   └── pm_launcher.c          category + app grid UI
├── components/
│   ├── pm_app_iface/          pm_app_t struct, category enum
│   ├── pm_audio/              NS4168 codec + I²S + PDM mic
│   ├── pm_board/              board profile (5/7 inch select)
│   ├── pm_boot/               POST screen + cyberpunk splash
│   ├── pm_bsp/                MIPI-DSI + GT911 + LVGL plumbing
│   ├── pm_c6_bridge/          legacy UART bridge to C6 (#if 0 reference)
│   ├── pm_c6_programmer/      C6 firmware flasher (future boards)
│   ├── pm_camera/             CSI camera abstraction
│   ├── pm_cardputer_i2c/      P4 ↔ Cardputer UART1 bridge (the spine)
│   ├── pm_gps_state/          shared GPS fix cache
│   ├── pm_gps_uart/           P4-direct NMEA parser (parked)
│   ├── pm_hal/                logging, SPI Treaty, NVS, time
│   ├── pm_input/              unified input dispatcher
│   ├── pm_lora/               SX1262 backend (RadioLib wrapper)
│   ├── pm_nfc/                PN532 via C6 bridge
│   ├── pm_nosql/              KV store on SD
│   ├── pm_peer/               MODULAR PEER REGISTRY
│   ├── pm_radio/              wireless slot auto-detect
│   ├── pm_sqlite/             SQLite wrapper
│   ├── pm_tbeam/              T-Beam Supreme S3 peer skeleton
│   ├── pm_ui/                 widget kit + pm_app_layout helper
│   ├── sqlite3/               vendored sqlite3.c
│   └── pm_apps/               THE APPS (56 across 7 categories)
│       ├── pm_apps_comms/     COMMS (6)
│       ├── pm_apps_cyber/     CYBER (19)
│       ├── pm_apps_games/     GAMES (7)
│       ├── pm_apps_intel/     INTEL (7)
│       ├── pm_apps_media/     MEDIA (3)
│       ├── pm_apps_system/    SYSTEM (9)
│       └── pm_apps_tools/     TOOLS (6)
├── docs/
│   └── MESHTASTIC_WARDRIVE_MAPPING.md
├── partitions.csv
├── sdkconfig
├── sdkconfig.defaults
└── CMakeLists.txt
```

### Critical components, by importance

**`pm_peer`** is the spine. Every multi-hardware capability flows
through it.

**`pm_cardputer_i2c`** is the canonical proof-of-concept peer. Look
here when implementing other UART/bus-attached peers.

**`pm_app_iface`** defines the app contract. Every app implements
`pm_app_t`. Do not deviate.

**`pm_ui`** + **`pm_ui_p4.h`** + **`pm_app_layout.h`** are the UI kit.
Use them. Do not hand-roll widgets unless a layout is genuinely
unique.

**`pm_boot`** owns the boot visuals. POST screen + splash. Phase 16
implementation is professional but needs **more cyberpunk** (see
roadmap §16).

---

## 12. App System

### Categories

```c
typedef enum {
    PM_CAT_SYSTEM,
    PM_CAT_TOOLS,
    PM_CAT_INTEL,
    PM_CAT_GAMES,
    PM_CAT_MEDIA,
    PM_CAT_COMMS,
    PM_CAT_CYBER,
} pm_cat_t;
```

### Registration

Every app exports a function `const pm_app_t* pm_app_<name>(void)`.
`main/pm_apps_register.c` calls every one of them at boot and stores
the returned pointers. The launcher reads from this list.

### App count by category (current)

| Category | Count | Notes |
|---|---|---|
| SYSTEM | 9 | about, files, filemgr, c6_flasher, gamepad, bridge, micropython, elf_browser, system |
| TOOLS | 6 | notepad, calculator, calendar, clock, etch, keytest, camera_qr |
| INTEL | 7 | terminal, ssh, gemini_log, ref_med, ref_surv, trails, baseball |
| GAMES | 7 | snake, pacman, galaga, chess, doom, simcity, retro_elf |
| MEDIA | 3 | audio_player, audio_recorder, camera |
| COMMS | 6 | gps, wifi, bluetooth, lora_voice, mesh_messenger, voice_terminal |
| CYBER | 19 | wardrive, bt_radar, pkt_sniffer, beacon, net_scanner, hash_tool, ble_gatt, wpa_hs, rf_spectrum, probe_intel, pkt_analysis, ble_ducky, usb_ducky, wifi_ducky, nfc_reader, nfc_clone, nfc_emulate, amiibo, secondary_scan, clinician, silas_creek |
| **TOTAL** | **57** | (one app moved between Phase 15 and Phase 16) |

### UI status by category (Phase 16 audit)

**Real UIs:** wardrive (Phase 16 redesign), clinician, silas_creek,
mesh_messenger (Phase 16 redesign), wifi, all games, notepad,
calculator, etch, clock, keytest, calendar, files, c6_flasher,
audio_player, audio_recorder, terminal, ssh, ref_browser-based apps,
camera, camera_qr, about, filemgr, micropython, gemini_log, ref_med,
ref_surv, trails, baseball.

**Real UI but needs P4 layout polish:** notepad, calculator, etch,
clock, calendar, files, c6_flasher (designed for smaller screens
originally).

**Likely stubs needing real UI (Phase 17 work):** bt_radar, pkt_sniffer,
pkt_analysis, hash_tool, probe_intel, wpa_hs, ble_gatt, ble_ducky,
usb_ducky, wifi_ducky, beacon, net_scanner, rf_spectrum, secondary_scan,
nfc_reader, nfc_clone, nfc_emulate, amiibo, gps, bluetooth, lora_voice,
voice_terminal.

### Shared UI infrastructure (Phase 16)

`pm_app_layout` is the Dashboard layout helper used by all redesigns.
Pattern:

```c
pm_app_layout_t L = {0};
pm_app_layout_begin(&L, "GPS NAVIGATOR");
pm_app_layout_chip(&L, "FIX", PM_LAYOUT_COL_OK);
pm_app_layout_chip(&L, "8 SATS", PM_LAYOUT_COL_ACCENT);
pm_app_layout_stats_row(&L, 6);
pm_app_layout_stat(&L, "LAT", "37.7749");
// ...
pm_app_layout_content(&L);
lv_obj_t* left = pm_app_layout_pane(&L, 320, "LIST");
lv_obj_t* right = pm_app_layout_pane(&L, 0, "DETAIL");
pm_app_layout_action(&L, "START", PM_LAYOUT_COL_OK, _start_cb);
s_screen = pm_app_layout_end(&L);
```

Result: titlebar with back button + chips, optional stats row, content
area with panes, action bar with colored buttons. ~80 lines of app
code instead of ~400.

---

## 13. UI Layer

### Stack
- LVGL 9.x as the rendering engine
- `lvgl_port` integration with `esp_lcd_ek79007` driver
- Single full-screen framebuffer in PSRAM (1024 × 600 × 2 bytes =
  ~1.2 MB) — correct for this panel; do not change to dual-buffer
- LVGL task **pinned to UI Core** (P4 HP Core 1)

### Design system

**Color tokens** (from `pm_theme.css` web reference, mirrored in
`pm_ui_p4.h` and `pm_app_layout.h`):

```
--bg:           #060d14  (very dark blue-black)
--bg2:          #0a1520  (panel backgrounds)
--bg3:          #0f1e2c  (cards / tiles)
--panel-border: #1f4060

--accent:       #4dd9ff  (cyan — data, highlights)
--accent2:      #ffa040  (warm orange — cloud/external)
--accent3:      #4dffa6  (fresh green — success, online)
--accent4:      #ff5577  (lifted red — threat, danger)
--warn:         #ffe066  (bright yellow)
--gold:         #ffd166  (luminous gold — field/navigation)
--purple:       #c89eff  (lifted purple — games, creative)

--text:         #c8e6f5  (default body)
--text-bright:  #ffffff  (primary headings)
--text-mid:     #8db8d0  (secondary)
--text-dim:     #5a8aa4  (tertiary)
```

**Typography:** Montserrat font sizes 10, 12, 14, 16, 18, 20, 24, 28, 36
enabled in sdkconfig. Larger sizes for stats; smaller for labels.

### Cyberpunk visual language (TODO — see roadmap)

The Phase 16 boot splash and launcher render correctly but lack
sufficient **cyberpunk identity**. The S3 reference implementations
(`launcher.cpp`, `main.cpp` showRainbowSplash) demonstrate the
intended aesthetic:

- Circuit-board grid background (20 px grid + PCB trace runs + solder
  pads)
- Octagonal/chamfered frames with bright green borders
- DIP-package chip icon with multi-color pin tips
- Rainbow per-character title cycling
- Chamfered hexagonal tiles for category/app grid
- Icon-trace stubs at tile edges making them look "wired into the
  circuit"
- Category-colored accents

**This work is on the Phase 17 roadmap.** Phase 16 prioritized
function over polish.

### Layout primitives

`pm_app_layout.h` (Phase 16):
- `pm_app_layout_begin/end` — root screen + titlebar
- `pm_app_layout_chip` — titlebar chips for status
- `pm_app_layout_stats_row` + `pm_app_layout_stat` — KPI cells
- `pm_app_layout_content` — main content area
- `pm_app_layout_pane` — left/right/wide panels
- `pm_app_layout_action` — bottom action bar with colored buttons

Adapts automatically between 5-inch (800×480) and 7-inch (1024×600).

### Input

`pm_input` is the unified dispatcher.

Sources:
- `PM_INPUT_SRC_TOUCH` — GT911 capacitive
- `PM_INPUT_SRC_CARDPUTER` — UART1 bridge
- `PM_INPUT_SRC_BLE_HID` — via C6
- `PM_INPUT_SRC_VIRTUAL_KEYBOARD` — on-screen LVGL keyboard
- `PM_INPUT_SRC_VIRTUAL_GAMEPAD` — on-screen LVGL gamepad

Apps subscribe with `pm_input_subscribe(cb, user)`. Foreground
filtering is the app's responsibility (compare `pm_app_current()->id`).

---

## 14. Build & Flash

### Toolchain

**ESP-IDF v5.5.3.** Locked. The project has been ported through IDF v5.4
and a brief experiment with v6.0.1 (which broke the `json` component
and several other things). **Stay on v5.5.3 unless explicitly upgrading
in a phase.**

### Board profiles

Two profiles supported, compile-time selected:

```sh
# 7-inch (default)
idf.py build
# → build/pisces_moon_p4.bin, 1024×600

# 5-inch
PM_P4_BOARD=elecrow_p4_5 idf.py -B build-p4-5 build
# → build-p4-5/pisces_moon_p4.bin, 800×480
```

Both currently build at ~2.27 MB, 64% flash free.

### Flash

```sh
# 7-inch
PORT=$(find /dev -name 'cu.usbmodem*' -print | head -1)
idf.py -p "$PORT" flash monitor

# 5-inch
PORT=$(find /dev -name 'cu.usbmodem*' -print | head -1)
PM_P4_BOARD=elecrow_p4_5 idf.py -B build-p4-5 -p "$PORT" flash monitor
```

### C6 firmware

Separate project at `pisces-moon-c6/` (sibling repo). Builds for
target `esp32c6`. Flash via the external UART1 console pin on the
CrowPanel — no disassembly required.

### Memory budget (Phase 16 build)

| Region | Used | Free | Total |
|---|---|---|---|
| PSRAM | 1.72 MB | ~30 MB | 32 MB |
| Internal SRAM | 334 KB (75%) | 111 KB | 445 KB |
| HP Core RAM | 104 B | ~8 KB | 8 KB |
| LP RAM | 56 B | ~32 KB | 32 KB |

Binary: ~1.85 MB. Watch internal SRAM — future subsystems should prefer
PSRAM allocation. LP RAM wide open for Sentinel Core (Phase 17).

---

## 15. Phase History

### Phase 14 and earlier
S3-family bring-up (Cardputer, T-Deck Plus, T-LoRa Pager). Original
firmware codebases that became the conceptual reference for P4.

### Phase 15 "Modular Peers"
First P4 work. Established `pm_peer` registry, `pm_radio` auto-detect,
NFC over C6 bridge, T-Beam Supreme S3 skeleton. 56 apps registered.
Build clean but unvalidated on hardware.

### Phase 16 "Live Iron" (current, 2026-05-26)
**The breakthrough phase.** Boot validated on real silicon.

Major accomplishments:
- Boot screen + splash rendering on physical hardware
- Launcher loads and apps launch
- M5 Cardputer ADV wired over UART1 as a live peer
- Keyboard, GPS, BLE all flowing through the bridge
- Wardrive runs WiFi (C6) + BLE (Cardputer) simultaneously
- Wardrive continues running in background after leaving app
- Mesh Messenger UI redesigned (Phase 16 dashboard pattern)
- `pm_app_layout` shared helper introduced
- 10+ apps ported from stubs to real UIs
- Snake watchdog fix (conditional virtual gamepad overlay)
- Backlight pulse regression eliminated
- T-Beam one-shot probe pattern (no more polling absent hardware)
- CSV-first storage for wardrive (10–100× faster than SQLite)
- 12 distinct bug classes identified and patched
- 5-inch and 7-inch builds both compile clean

### Phase 17 "Cyberpunk + Stabilization" (proposed)
See roadmap §16.

---

## 16. Roadmap

### Phase 17 priorities (next session)

**Critical visual polish:**
1. **Cyberpunk theme restoration.** Rebuild `pm_boot.c` splash with
   circuit-board background, octagonal frame, chip icon with colored
   pins, rainbow title cycle. Rebuild `pm_launcher.c` with chamfered
   hexagonal tiles, PCB trace stubs at tile edges, category-colored
   accents, generous spacing (square tiles, visually striking).
   Reference: S3 `launcher.cpp` `drawChamferedBox`,
   `drawIconTraces`, `drawCyberpunkHeader`, and S3 `main.cpp`
   `showRainbowSplash`. RAM-sip target: <50 KB extra PSRAM for the
   launcher, no per-frame redraw on the splash.

**Critical functional:**
2. Validate Cardputer firmware LoRa command handling
   (`lora_mesh_start`, `lora_tx`, `lora_stop` + `PMU1 LORA` RX
   forwarding).
3. Validate Cardputer WiFi promiscuous forwarding
   (`PMU1 WF` lines).

**App UI polish:**
4. Per-app UI redesigns following the Phase 16 wardrive Dashboard
   pattern. Priority order:
   - GPS (web reference exists, big improvement available)
   - WiFi (already real, layout pass for 1024×600)
   - BT Radar
   - Notepad (already real, P4 layout pass)
   - BLE GATT
   - RF Spectrum
   - Net Scanner
   - Packet Analysis
   - WPA Handshake
   - Probe Intel

**System work:**
5. Status bar overlay (top of screen, persistent across apps)
6. Pull-down control center (WiFi toggle, brightness, notifications)
7. App switcher (recent apps strip)
8. `pm_sdlog` — queued CSV writer task (offload SD I/O from app threads)
9. Software PMU on LP Core (Sentinel Core finally used)
10. Rename `pm_cardputer_i2c` → `pm_cardputer_bridge` (cosmetic)

### Phase 18+ (hardware-blocked or longer term)

- Wireless module bring-up when SX1262/H2/C6/nRF24 arrive
- Custom C6 plug-in firmware for true promiscuous capture
- T-Beam Supreme S3 firmware (currently P4-side skeleton only)
- Reading apps (markdown, EPUB, PDF) — PSRAM has plenty of headroom
- Real audio playback pipeline (codec already wired; needs format
  decoders)
- Custom Pisces Moon video format (compressed for ESP32 playback)
- Networked GPS / mesh between devices (Cardputer + T-Deck + P4
  cooperating)

### Long-horizon

- Multi-edition product split:
  - Pisces Moon Cyber Edition (current focus)
  - Pisces Moon Audio Edition (audio-app-curated)
  - Pisces Moon Reference/Field Edition (offline reference apps)
  - Pisces Moon Developer Edition (REPL, debugging tools)
  - Pisces Moon Studio Edition (creative tools)
- Web-app references in `pisces-moon-apps.zip` to be ported when their
  category lights up (offline maps, body metrics, habits, watchtower,
  newsfeeds, vault, passgen, port scanner, etc.).

---

## 17. The Bible's Locked Conventions

These do not change without an explicit phase commit and Eric's
go-ahead.

### Architecture
- **ESP-Hosted over SDIO** is the C6 transport. The legacy UART
  `pm_c6_bridge` remains in `#if 0` blocks as reference.
- **OS Core / UI Core / Sentinel Core** naming. Ghost Engine is the
  external Core 0 (the C6 chip).
- **LVGL is pinned to UI Core.** All UI runs there. Apps' background
  workers pin to OS Core.
- **Apps lazy-initialize on first enter.** Never at registration.
- **Modular peripherals probe once at boot.** Absent hardware shuts
  down cleanly. Hot-plug requires reboot.
- **One wireless module at a time.** The slot is exclusive per session.
- **CSV/JSONL for firehose logging.** SQLite for queryable user data
  only.

### Toolchain
- **ESP-IDF v5.5.3.** Do not silently upgrade. Project survived a brief
  v6.0.1 experiment that broke `json` component and other dependencies.

### Hardware
- **5-inch and 7-inch profiles both compile clean.** Any change must
  preserve both.
- **GPS is currently from the Cardputer over UART1.** The P4-direct
  BN-180 is parked. Future re-enable possible when diagnostic tools
  available (USB-serial adapter or multimeter to confirm UBX/baud).
- **`PM_BOARD_LCD_H_RES`** is the canonical screen-size check.

### Code style
- **Pure C with `pm_` namespace.** No C++ except `pm_lora.cpp` (one
  TU, the RadioLib wrapper).
- **SPDX header on every source file.**
- **AGPL-3.0-or-later.** Period.
- **Forward-declare static functions.** No implicit declarations.
- **`-Werror` build.** Strict warnings as errors per component, with
  documented relaxations.

### Identity
- **Always-on portable computing device.** Not an IoT logger. Not
  sleep-targeting.
- **Pisces Moon is open-source.** Public Github, public docs,
  fluidfortune.com.
- **Eric Becker / Fluid Fortune is the maintainer.** Contributions via
  CLA.

---

## 18. The Multi-Agent Pipeline

Pisces Moon is developed by a one-human multi-AI team. The roles are:

### Eric (human, project lead)
- Strategic decisions
- Architecture choices
- Hardware procurement and bench validation
- Final approval on phase transitions
- The taste filter on every visual decision

### Claude Desktop (architectural Claude)
- Project Bible (this doc) and phase changelogs
- Architecture review
- Design specifications
- Debugging methodology
- **Direct filesystem access (Phase 16+).** Edits live files in the
  repo. No more paste-back loops.
- Should always: present the list of files changed after each task,
  read files fresh before editing, never delete files without explicit
  permission.

### Claude Code / Codex (compile-driven Claudes)
- Bulk mechanical fixes
- Compile-error iteration in sandboxed builds
- High-volume refactoring within architectural constraints
- Bounded scope tasks (one file or one component at a time)
- Should never: touch the architectural conventions in §17 without an
  explicit phase change.

### What Codex / Code is **not allowed** to do without explicit permission

- Restore the deprecated UART `pm_c6_bridge` as active code (it's
  reference only)
- Normalize storage to SQLite-everywhere (CSV-first is locked)
- Replace `pm_peer` with conditional compilation
- Modify `sdkconfig.defaults` rev settings (correct for v1.3 silicon)
- Change `pm_bsp.c` framebuffer config (single full-screen PSRAM
  buffer is correct for this panel)
- Restore continuous polling for absent peripherals
- Touch the apps from S3 lineage that have been ported and validated
  (calculator, calendar, notepad, snake, chess, galaga, pacman)

### Communication protocol between agents

- **Project Bible (this doc)** is the contract. If an instruction
  contradicts the Bible, the Bible wins.
- **Latest phase changelog** is "what happened this session."
- **Handoff docs** at phase transitions cover what's in progress.
- **Inline code comments** explain decisions, especially `// LOCKED:`
  comments that mark architectural commitments.

---

## 19. Open Work and Known Issues

### Confirmed working (Phase 16, on hardware)
- P4 boots cleanly to launcher
- Boot screen (POST-style) renders
- Cyberpunk splash renders (style needs more cyberpunk — see §16)
- Cardputer ADV UART bridge detected
- Cardputer keyboard input
- Cardputer GPS fixes
- Cardputer BLE scanning
- Wardrive WiFi (C6) + BLE (Cardputer) simultaneously
- Wardrive background multitasking
- Notepad keyboard input + SD save
- Snake without watchdog
- 5-inch and 7-inch builds both compile

### Pending hardware validation
- Cardputer LoRa command handling (P4 side coded; firmware unconfirmed)
- Cardputer WiFi promiscuous forwarding
- Mesh LoRa fallback RF traffic
- Wireless module auto-detect (modules not yet on bench)

### Known issues
- `pm_cardputer_i2c` log strings still say "I2C Module" — cosmetic,
  Phase 17 rename to "Cardputer ADV Bridge"
- BN-180 GPS on IO52 produces incoherent data; parked pending diagnostic
  tools (USB-serial adapter or multimeter required for definitive
  diagnosis)
- Several apps still use Phase 15 default-screen placeholder instead
  of `pm_app_layout` — per-app UI work is Phase 17
- Boot splash is functional but not yet "cyberpunk enough"
- Launcher tile grid is functional but visually conservative — needs
  chamfered tiles + PCB traces (Phase 17)
- Internal SRAM at 75% — future allocations should prefer PSRAM

### Recently fixed (Phase 16)
- `_boot_visual_probe` LEDC pin theft → neutralized
- 8-second boot heartbeat loop → removed
- Wardrive 6-second freeze on open → deferred DB init
- SQLite firehose overhead → CSV-first default
- Brace-less `_enter` functions in 15 apps
- Missing `esp_wifi`/`esp_event` in cyber CMakeLists
- Missing `pm_boot` in main CMakeLists
- Mangled multi-line C strings from heredoc patches
- C++ lambda syntax in `.c` files
- T-Beam continuous polling when absent
- Missing Montserrat font sizes

---

## 20. Glossary

- **AP / STA** — Access Point / Station mode (WiFi).
- **BLE** — Bluetooth Low Energy.
- **Bridge** — UART1 link between P4 and Cardputer ADV. The canonical
  modular-peer demonstration.
- **C6 / Ghost Engine** — The ESP32-C6 coprocessor on the CrowPanel
  board. Runs always-on radio firmware over ESP-Hosted SDIO.
- **Cardputer** — M5 Cardputer ADV (ESP32-S3) acting as a peer to the
  P4 over UART1.
- **CrowPanel Advanced 7"** — The ELECROW reference board, the primary
  P4 development target.
- **Capability** — A string-keyed thing a peer can do (e.g. `lora_tx`,
  `gps_status`).
- **Dashboard layout** — The Phase 16 P4 UI pattern: titlebar + chips,
  optional stats row, content panes, action bar.
- **ESP-Hosted** — Espressif's protocol for using a coprocessor's
  radios over SDIO. The C6 runs ESP-Hosted firmware; the P4 is the
  host.
- **Ghost Engine** — Codename for the C6 coprocessor firmware. Owns
  WiFi station, BLE scan, promiscuous mode.
- **HMI** — Human-Machine Interface (Elecrow's term for their
  touchscreen boards).
- **LongFast** — Default Meshtastic channel (US: 906.875 MHz).
- **LP Core** — Low-power RISC-V core on the P4. Designated Sentinel
  Core. Currently unused.
- **MIPI-DSI / MIPI-CSI** — Display Serial Interface / Camera Serial
  Interface, the high-speed display and camera buses.
- **NMEA** — Standard text-line protocol for GPS data.
- **NoSQL** — `pm_nosql`, a key-value blob store on SD. Tier 3 storage.
- **OS Core** — P4 HP Core 0. Pisces Moon kernel runs here.
- **Peer** — Any hardware that registers with `pm_peer`. Can be the C6,
  the Cardputer, a wireless slot module, a CSI camera, etc.
- **PMU** — Power Management Unit. The hardware one (AXP2101 on some
  boards) and the future software one on the LP core.
- **PSRAM** — Pseudo-static RAM. The P4 has 32 MB.
- **Sentinel Core** — Reserved name for the P4 LP core's eventual role
  as a software PMU watchdog.
- **SDIO** — Secure Digital I/O bus. Used by the C6 (ESP-Hosted) and
  the SD card.
- **SPI Treaty** — Pisces Moon's invented synchronization pattern for
  shared SPI buses with multiple owners (radio + SD).
- **Sx1262 / nRF24 / H2** — Wireless slot module candidates.
- **T-Beam Supreme S3** — Optional secondary radio peer (LilyGo board)
  attached via P4 UART2 ping-handshake.
- **UI Core** — P4 HP Core 1. LVGL is pinned here.
- **Wardrive** — The flagship CYBER app. WiFi + BLE capture with GPS
  geo-tagging. Phase 16 hero app.
- **Wireless slot** — The module bay on the CrowPanel board accepting
  SPI-based radio modules.

---

## Closing

Pisces Moon is the OS the modular-hardware ecosystem deserved and
nobody built. It's open-source. It's real, on real silicon, today. The
hard parts — modular peer abstraction, multi-device bridging, the
always-on philosophy — work in production.

What remains is polish, more apps, more devices, and the long tail of
field validation. The platform is real. The pipeline works.

— Eric Becker / Fluid Fortune
fluidfortune.com
