<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Pisces Moon OS — Architecture Reference

**Complete guide to the OS, language, build, apps, and dual-MCU design**

Version: v1.2.0-alpha "Pisces Moon P4"
Author: Eric Becker / Fluid Fortune
License: AGPL-3.0-or-later
Web: [fluidfortune.com](https://fluidfortune.com)

---

## Contents

1. [What Pisces Moon is and what it isn't](#part-1--what-pisces-moon-is-and-what-it-isnt)
2. [Hardware overview](#part-2--hardware-overview)
3. [Why C, not C++](#part-3--why-c-not-c)
4. [ESP-IDF: the build system and framework](#part-4--esp-idf-the-build-system-and-framework)
5. [System architecture: the components](#part-5--system-architecture-the-components)
6. [The dual-MCU design (P4 + C6)](#part-6--the-dual-mcu-design)
7. [S3 vs P4/C6: what changed and why](#part-7--s3-vs-p4c6-what-changed-and-why)
8. [Bridge protocol between P4 and C6](#part-8--bridge-protocol-between-p4-and-c6)
9. [The 56 applications](#part-9--the-56-applications)
10. [The radio slot architecture](#part-10--the-radio-slot-architecture)
11. [Storage: SD, SQLite, and NoSQL](#part-11--storage-sd-sqlite-and-nosql)
12. [UI: LVGL and the Pisces UI kit](#part-12--ui-lvgl-and-the-pisces-ui-kit)
13. [Build, install, flash](#part-13--build-install-flash)
14. [C6 firmware: how it gets onto the chip](#part-14--c6-firmware-how-it-gets-onto-the-chip)
15. [The modular peer model](#part-15--the-modular-peer-model)
16. [The input layer](#part-16--the-input-layer)
17. [Phase 15 additions: NFC, camera, T-Beam](#part-17--phase-15-additions-nfc-camera-t-beam)
18. [Licensing and contribution](#part-18--licensing-and-contribution)

---

## Part 1 — What Pisces Moon is and what it isn't

### In one paragraph

Pisces Moon OS is an open-source, hackable operating system for a 7-inch touchscreen device built around the ESP32-P4 microcontroller, with a second ESP32-C6 microcontroller dedicated entirely to wireless work. It runs 56 applications across 7 categories — productivity, entertainment, communications, and security research tools — and is licensed AGPL-3.0-or-later so derivative work stays open. The OS is **modular**: a peer registry auto-detects optional hardware (NFC reader, T-Beam coprocessor, camera, BLE gamepad, wireless slot modules) at boot and routes app requests to whichever peer offers the requested capability.

### What it is

- A general-purpose desktop-style OS for a specific piece of hardware (the ELECROW CrowPanel Advanced 7" HMI)
- A 19,685-line firmware codebase split across two MCU targets: the P4 (UI + apps) and the C6 (radios)
- Successor to the original Pisces Moon OS for the LilyGO T-Deck Plus (ESP32-S3 platform); the v1.1.x branch remains supported on that hardware
- Built on ESP-IDF v5.4 (Espressif's official framework), LVGL v9 (the embedded GUI library), and several published Espressif drivers for display and touch
- AGPL-3.0-or-later: free to use, modify, and distribute; if you run a network service from it, you must publish your modified source

### What it is not

- Not Linux. There is no kernel in the conventional sense — this is bare-metal firmware running on an MCU, with FreeRTOS providing task scheduling
- Not portable. The pin numbers, bus layouts, and BSP code target one specific board
- Not a phone, even though it has a screen and radios. There's no cellular modem, no SIM slot, no telephony stack
- Not finished. This is an alpha. The full firmware tree builds, but several radio-side bring-up tasks await a hardware test
- Not a forensics tool. The cyber apps are research and educational instruments. Use only on networks and devices you own or have explicit permission to study

### Who it's for

- Hobbyists who want a hackable Linux-like experience on small embedded hardware
- Security researchers who need a portable wardriving / packet-capture / mesh-radio platform
- Embedded developers who want a real ESP-IDF reference codebase showing component organization, app frameworks, dual-MCU bridge protocols, and LVGL UI patterns
- Anyone who likes the idea of a tiny, all-AGPL, all-open device that does real work and doesn't phone home

---

## Part 2 — Hardware overview

### The board

The reference target is the ELECROW CrowPanel Advanced 7" ESP32-P4 HMI Display (model number DHE04107D, board revision V1.0). It's a single-board computer about the size of a paperback novel, with a 1024×600 touchscreen on the front and most of the components on the back.

Key chips and components on the board:

| Component | Role |
|---|---|
| **ESP32-P4-NRW32** | Main MCU. Dual-core RISC-V LX9 at 360 MHz. 32 MB PSRAM, 16 MB flash. |
| **ESP32-C6-MINI-1** | Coprocessor for WiFi 6 / BLE 5 / 802.15.4. Runs custom Ghost Engine firmware. |
| **ILI9881C** | Driver IC for the 1024×600 IPS panel, fed via MIPI-DSI 2-lane bus. |
| **GT911** | Capacitive touch controller, on I2C. Reports up to 5 simultaneous touch points. |
| **NS4168** | Stereo audio codec / amplifier. Drives two onboard speakers via I2S. |
| **BN-180** | Beitian GPS module on a UART. Provides standard NMEA sentences. |
| **MicroSD slot** | SDIO 1-bit. Used for SD storage and as the home of wardrive session DBs. |
| **Wireless module slot** | ELECROW's proprietary slot for interchangeable carriers (SX1262, nRF24, ESP32-H2, ESP32-C6 slot variant, Wi-Fi HaLow). |
| **Two USB-C ports** | One for power and programming ("PWR / 382.0"), one for HID device-mode. |
| **MIPI-CSI camera port** | Unused in current firmware; wired for a future camera app. |

### The CrowPanel-specific gotchas

- The C6 is connected to the P4 via an internal SDIO bus; ELECROW expects you to leave the C6 on its stock firmware. The board does **NOT** expose the C6 console pins externally. Custom Ghost Engine firmware therefore reaches the C6 via a P4-mediated SDIO flash path (not yet brought up).
- The wireless module slot uses a proprietary footprint. Generic SX1262 carriers like the Waveshare Core1262-HF do **NOT** fit. ELECROW sells matching $6.55 carriers.
- The 2×12 GPIO breakout header on the side gives access to power rails (3V3, 5V), grounds, and a handful of free GPIOs (IO2, IO3, IO4, IO5, IO27, IO28, IO49, etc.).
- The Beitian BN-180 GPS is wired to GPIO breakout pins IO2/IO3 in factory delivery — a P4-direct connection, **NOT** routed through the C6. This is why the firmware parses GPS NMEA on the P4 directly rather than via the bridge.

---

## Part 3 — Why C, not C++

Pisces Moon v1.0 / v1.1 (the S3 version) was written in C++ on the Arduino framework. The P4 port deliberately switches to C on ESP-IDF. There are real reasons.

### Predictability

C has very few hidden costs. When you write a function call, that's a function call. When you allocate memory, you're calling `malloc`. There's no copy constructor secretly running, no implicit destructor, no template instantiation creating thousands of bytes of code you didn't ask for. On an MCU with 32 MB of PSRAM and 16 MB of flash, this matters: every kilobyte of code is a kilobyte you have to fit, every cycle is a cycle that isn't doing your work.

### ESP-IDF is C-first

Espressif's official framework is written in C, documented in C, and most of its examples are C. Component dependencies, log macros, FreeRTOS APIs, the driver layer, all of it: C. You can use C++ on top — and Pisces Moon does, for one specific file (the RadioLib SX1262 wrapper) — but going C-first means you're swimming with the current. Less impedance mismatch with the framework you're building on.

### Smaller binaries, faster builds

C++ templates and the Standard Template Library (STL) are powerful but they generate large amounts of code, particularly on chips that don't have great library support. The S3 version of Pisces Moon was running into compile times of several minutes and binary sizes that were starting to crowd the partition. Switching to C cuts both substantially. A clean C build of the P4 firmware compiles in 5–10 minutes the first time, ~30 seconds incremental — and the binary is smaller for it.

### Sharper interfaces

C doesn't let you hide complexity inside operator overloads or implicit conversions. If something is happening, you can see it in the code. This forces clean APIs: `pm_app_t` is just a struct of function pointers, `pm_radio_take` is just a function, `pm_lora_tx` is just a function. There's no inheritance hierarchy to navigate, no virtual dispatch, no surprise.

When you're debugging an MCU with a stack trace that's hex addresses, this matters. You want function pointers to point to functions that do what they're named, and you want stack frames to be predictable.

### The one C++ exception

RadioLib (the SX1262 driver library) is a C++ library and there's no good C alternative for the breadth of radio chips it supports. Pisces Moon's `pm_lora.cpp` is the only C++ translation unit in the P4 firmware: a thin wrapper that exposes a clean C ABI to the rest of the codebase via `extern "C"` declarations. Apps never see RadioLib types. The rule is: **cross the C/C++ boundary once, at one well-defined place, and put everything else on the C side.**

> **The Bus Treaty:** The S3 codebase introduced a discipline called the SPI Bus Treaty: any access to the shared SPI bus must take the bus mutex first. The P4 port preserves this. The `pm_hal` component exposes `pm_spi_take`/`pm_spi_give` and a `PM_SPI_TAKE("who")` macro, used everywhere the radio, SD card, or other SPI peripherals are touched. The naming makes it visible in code; the discipline made the S3 wardrive stable through 100+ hours of capture.

---

## Part 4 — ESP-IDF: the build system and framework

### What ESP-IDF is

ESP-IDF (the Espressif IoT Development Framework) is the official software development kit for all ESP32 chips. It provides:

- A C compiler toolchain (`xtensa-esp-elf-gcc` for older chips, `riscv32-esp-elf-gcc` for the P4)
- FreeRTOS with multi-core support
- Drivers for every peripheral: GPIO, SPI, I2C, UART, I2S, SDMMC, MIPI-DSI, etc.
- WiFi and Bluetooth stacks (used on the C6 side)
- A component-based build system layered on top of CMake
- `idf.py` — a Python wrapper that drives the build, flash, and monitor cycle
- A managed component registry for pulling in third-party libraries (LVGL, RadioLib, panel drivers)

### Components

ESP-IDF's organizing principle is the **component**. A component is a folder containing a `CMakeLists.txt` that calls `idf_component_register()` to declare its source files, header search paths, and dependencies on other components. The build system walks the component graph, compiles each one, and links the final binary.

Pisces Moon P4 has 19 internal components plus several managed components from the registry:

| Component | Purpose |
|---|---|
| `pm_hal` | Low-level helpers: log macros, time, NVS, CRC32, SPI Treaty, PSRAM allocators |
| `pm_bsp` | Board Support Package: MIPI-DSI bus, GT911 touch, backlight, LVGL plumbing |
| `pm_ui` | Pisces UI kit: theme tokens, widget builders, default-screen template, on-screen keyboard, virtual gamepad |
| `pm_input` | Unified input dispatcher (Phase 14) — touch + BT HID + virtual widgets |
| `pm_peer` | Modular peer registry (Phase 15) — capability-based hardware routing |
| `pm_app_iface` | `pm_app_t` struct, category enum, launcher API |
| `pm_audio` | NS4168 codec setup, I2S buffer chains, PDM mic |
| `pm_nosql` | Simple file-based key-value store on SD |
| `pm_sqlite` | Wardrive session DB — wraps `siara-cc/sqlite3` managed component |
| `pm_gps_state` | Shared GPS fix cache with versioned snapshot |
| `pm_gps_uart` | P4-direct NMEA parser for the BN-180 GPS |
| `pm_radio` | Wireless slot abstraction with auto-detect + SX1262/nRF24 backends |
| `pm_lora` | RadioLib C++ wrapper exposing C API (the only C++ TU) |
| `pm_nfc` | NFC API (Phase 15) — receives PN532 events from C6 bridge |
| `pm_camera` | CSI camera component (Phase 15) — wraps `esp_video` managed component |
| `pm_tbeam` | T-Beam Supreme S3 secondary radio peer (Phase 15) — UART2 bridge |
| `pm_c6_bridge` | UART bridge to the C6 with cJSON event dispatch |
| `pm_c6_programmer` | ESP serial bootloader protocol (future-board SDIO flasher) |
| `pm_apps/system` | 9 SYSTEM apps (about, files, filemgr, c6_flasher, etc.) |
| `pm_apps/tools` | 6 TOOLS apps (Phase 15 adds Camera QR scanner) |
| `pm_apps/intel` | 7 INTEL apps |
| `pm_apps/games` | 7 GAMES apps |
| `pm_apps/media` | 3 MEDIA apps (Phase 15 adds Camera) |
| `pm_apps/comms` | 6 COMMS apps |
| `pm_apps/cyber` | 19 CYBER apps (Phase 15 adds 4 NFC apps + Secondary Scan) |

### Managed components from the registry

These come from `components.espressif.com` and are pulled at build time:

| Package | Version | Purpose |
|---|---|---|
| `lvgl/lvgl` | `^9.2.0` | LVGL embedded GUI library v9 |
| `espressif/esp_lvgl_port` | `^2.4.0` | LVGL ↔ esp_lcd integration |
| `espressif/esp_lcd_ili9881c` | `^1.0.0` | 1024×600 MIPI-DSI panel driver |
| `espressif/esp_lcd_touch` | `^1.1.0` | Touch driver framework |
| `espressif/esp_lcd_touch_gt911` | `^1.2.0` | GT911 capacitive touch driver |
| `jgromes/radiolib` | `^7.2.1` | SX1262 / nRF24 / many-radio C++ driver library |
| `siara-cc/sqlite3` | `^0.4.0` | SQLite for ESP-IDF (wardrive session DBs) |

### The build / flash / monitor cycle

Day-to-day work on ESP-IDF revolves around three commands wrapped by VS Code's "ESP-IDF: Build, Flash and start a Monitor":

```bash
idf.py build               # compile sources to a binary image
idf.py flash               # upload the binary to the chip
idf.py monitor             # open serial monitor, see log output

# in one shot:
idf.py -p /dev/ttyUSB0 flash monitor
```

The first build pulls 4 GB of toolchain + managed components and takes 5–10 min. Incremental builds (just code changes) take ~30 seconds. The bring-up guide walks through this in detail.

---

## Part 5 — System architecture: the components

Pisces Moon's internal architecture is a layered stack. Each layer is a separate ESP-IDF component, each component has a clean API, and apps sit on top of all of it.

### The boot sequence

From cold reset to launcher visible takes about 2 seconds. The order:

```
app_main()  in main/main.c
  1. pm_hal_init()
       - log subsystem, NVS, PSRAM, SPI Treaty mutex
  2. pm_sqlite_global_init()
       - SD card mount, /sd/sessions/ directory check
  3. pm_bsp_init()
       - I2C bus -> GT911 touch -> backlight 80% -> MIPI-DSI panel
       - LVGL display + indev registration
  4. pm_gps_uart_init()
       - UART1 on IO2/IO3, NMEA parser task spawned
  5. pm_radio_init_auto()
       - SPI signature probes, detect SX1262 / nRF24 / NONE
  6. pm_launcher_init()
       - LVGL category screen + per-category app screens
  7. main_register_apps()
       - 49 apps registered via REGISTER_IF()
  8. lv_screen_load(category screen)
       - first frame visible to user
```

### App lifecycle

Every app implements the `pm_app_t` interface:

```c
typedef struct {
    const char*    id;             // "clock", "wardrive", etc.
    const char*    display_name;   // shown in tile
    pm_category_t  category;       // SYSTEM/TOOLS/INTEL/...
    int            icon_id;
    void  (*init)(void);           // called once at boot
    void  (*enter)(void);          // tile tapped
    void  (*tick)(uint32_t ms);    // periodic update while open
    void  (*exit)(void);           // back tapped
    void  (*deinit)(void);         // shutdown (unused so far)
} pm_app_t;
```

The launcher walks the registered app array, builds tiles, and calls `enter()`/`exit()` as the user navigates. App state is private to each app's translation unit; cross-app data flows through `pm_nosql`, `pm_gps_state`, or the C6 bridge.

### Categories

| Category | Apps | Examples |
|---|---|---|
| SYSTEM | 9 | About, Files, FileMgr, C6 Flasher, Bridge |
| TOOLS | 5 | Notepad, Calculator, Clock, Calendar, Etch |
| INTEL | 7 | Terminal, Gemini, Reference, Trails, SSH |
| GAMES | 7 | Snake, Pac-Man, Galaga, Chess, Doom, SimCity, RetroELF |
| MEDIA | 2 | Audio Player, Audio Recorder |
| COMMS | 6 | GPS, WiFi, Bluetooth, LoRa Voice, Mesh, Voice Terminal |
| CYBER | 14 | Wardrive, BT Radar, Pkt Sniffer, WPA HS, RF Spectrum, Ducky x3 |

---

## Part 6 — The dual-MCU design

The CrowPanel has two ESP32 chips on one board. Pisces Moon uses both, deliberately separated.

### Why two chips?

On the original S3 platform, every radio scan, every game frame, every SD write was competing for the same CPU. The wardrive task had to yield. The UI got janky during heavy capture. Audio sometimes glitched.

The CrowPanel hardware solves this by physically separating the two concerns. The P4 (faster, more memory, dedicated peripherals for display and audio) handles user-facing work. The C6 (smaller, but with native WiFi 6 / BLE 5 / 802.15.4 radios) handles wireless work.

Pisces Moon takes that hardware split and runs with it. **The C6 doesn't merely supplement the P4's wireless — it owns wireless entirely.** The P4 has no WiFi, no Bluetooth, and asks the C6 for everything radio-related via UART.

### What runs where

| Subsystem | Runs on |
|---|---|
| UI / launcher / apps | P4 |
| Display (MIPI-DSI) | P4 |
| Touch (GT911) | P4 |
| Audio (NS4168) | P4 |
| GPS (BN-180) | P4 (direct UART) |
| SD storage | P4 |
| LoRa / nRF24 / wireless slot | P4 (via SPI bus) |
| WiFi scanning / capture | C6 |
| BLE scanning / GATT / HID | C6 |
| 802.11 promiscuous mode | C6 |
| WPA EAPOL collection | C6 |
| HTTP client (the P4 has no IP stack) | C6 |
| RF spectrum sweep | C6 |
| Ghost Engine wardrive task | C6 |

### The Ghost Engine identity

The C6 firmware is called the **Ghost Engine**. It's deliberately minimal: no UI, no apps, no UI framework. Just radios. It boots, starts the WiFi and BLE scanners, optionally enables promiscuous mode, and continuously emits structured events to the P4 over UART.

This is the project's primary architectural distinguishing feature. An always-on, dedicated, non-stoppable radio MCU is something single-chip designs can approximate but cannot truly replicate. The user can reboot the P4 — the C6 keeps wardriving. The user can play Doom — the C6 keeps wardriving. This guarantee is what makes the wardrive logs trustworthy and what makes Pisces Moon distinct from "just an OS that has WiFi."

> **AGPL implication:** The dedicated-radio-MCU architecture is the kind of design choice the AGPL is meant to protect. Anyone forking this work and running it as a service must publish their modified C6 firmware under AGPL. This is intentional: the project's identity is in the architecture, and the license keeps that architecture open.

---

## Part 7 — S3 vs P4/C6: what changed and why

The S3 (T-Deck Plus) version of Pisces Moon was the first version. It worked. People used it. v1.1.x is still maintained. So why a rewrite for the P4?

### Hardware differences

| | S3 (T-Deck Plus) | P4/C6 (CrowPanel Adv 7") |
|---|---|---|
| Form factor | Pocket handheld with keyboard | 7" tablet form, all touch |
| Main MCU | ESP32-S3 (Xtensa) | ESP32-P4 (RISC-V) |
| RAM | 8 MB PSRAM | 32 MB PSRAM |
| Flash | 16 MB | 16 MB |
| Display | 2.8" 320×240 SPI | 7" 1024×600 MIPI-DSI |
| Display bandwidth | ~2 MB/s SPI | ~50 MB/s MIPI-DSI |
| Input | Trackball + 47 keys + touch | Capacitive touch only |
| Audio | Mono speaker | Stereo speakers + PDM mic |
| Coprocessor | None | ESP32-C6 |
| GPS | Optional add-on | Onboard BN-180 |
| LoRa | Onboard SX1262 | Pluggable wireless slot |

### Software differences

| | S3 | P4/C6 |
|---|---|---|
| Language | C++ / Arduino | C / ESP-IDF (one C++ TU) |
| Framework | Arduino-ESP32 | ESP-IDF v5.4 |
| Build | PlatformIO | ESP-IDF / VS Code extension |
| UI | Custom framebuffer + widgets | LVGL v9 |
| Storage | SPI Treaty + ad-hoc files | SPI Treaty + SQLite + NoSQL |
| App model | switch() in main loop | pm_app_t struct, registered list |
| Wardrive | Pauses on app launch | Always-on, dedicated MCU |
| GPS | Reads NMEA on main MCU | Reads NMEA on main MCU (different UART) |
| Source files | ~50 .cpp/.h | 200 across two trees |
| Total lines | ~14,000 | 22,500 |

### What stayed the same

- The 56-app catalog (49 original + 7 Phase 15 modular). Every app from S3 made the jump.
- The category structure (SYSTEM, TOOLS, INTEL, GAMES, MEDIA, COMMS, CYBER).
- The SPI Bus Treaty discipline. Every component that touches the shared SPI bus takes the mutex first.
- The Pisces palette: deep sea blue, moonlight white, teal moon accent.
- The AGPL-3.0-or-later license.
- The fluidfortune.com identity.

### What got better

- The wardrive is genuinely always-on now (because it runs on a dedicated chip).
- The display is enormous and high-resolution (1024×600 vs 320×240 — that's 6.4× the pixels).
- Audio is stereo and supports a PDM microphone.
- The component-based architecture makes the code far easier to navigate and extend.
- LVGL gives professional-feeling animations, theming, and input handling for free.
- Managed dependencies — RadioLib, LVGL, panel drivers — pull in via the registry.

### What got harder

- ESP-IDF has a steeper learning curve than Arduino. The bring-up guide exists for this reason.
- Custom C6 firmware requires the SDIO flasher (Phase 14).
- MIPI-DSI panels are sensitive to timing parameters; a wrong porch value gives a black screen.
- Two firmware trees means two build/flash cycles to keep in sync.

---

## Part 8 — Bridge protocol between P4 and C6

### Wire format

The P4 and C6 talk over a UART running at 921600 baud. Each packet is one JSON object terminated by a newline. The protocol is human-readable; you can hold it up in a serial monitor.

### Direction

Two sides:

- **P4 → C6:** commands, in the form `{"cmd":"...", ...args}`
- **C6 → P4:** events, in the form `{"event":"...", ...fields}`

### Examples

```json
// P4 sends:
{"cmd":"wifi_scan"}

// C6 streams events as networks are seen:
{"event":"wifi_seen","mac":"aa:bb:cc:dd:ee:ff","ssid":"home",
 "rssi":-67,"ch":6,"enc":"WPA2","lat":0.0,"lng":0.0}

// P4 sends:
{"cmd":"http_get","id":42,"url":"https://example.com"}

// C6 fetches and emits:
{"event":"http_response","id":42,"status":200,"truncated":false,
 "len":1234,"body_b64":"PGh0bWw+..."}
```

### Commands the bridge supports

| Command | Effect |
|---|---|
| `wardrive_start` / `wardrive_stop` | Toggle continuous WiFi scanning |
| `ble_start` / `ble_stop` | Toggle continuous BLE scanning |
| `promiscuous_start` / `promiscuous_stop` | Enter/leave 802.11 monitor mode |
| `promiscuous_filter` | Set frame-type bitmask (mgmt/data/ctrl) |
| `wifi_scan` / `wifi_connect` / `wifi_disconnect` | STA control |
| `http_get` / `http_post` | HTTP proxy with base64 response |
| `hid_pair` / `hid_string` / `hid_key` / `hid_disconnect` | BLE HID keyboard |
| `ble_connect` / `ble_read` / `ble_write` / `ble_disconnect` | BLE GATT central |
| `net_scan` | ARP-sweep host discovery on connected subnet |
| `rf_spectrum_start` / `rf_spectrum_stop` | Channel utilization sweep |
| `wpa_hs_start` / `wpa_hs_stop` | EAPOL handshake collector |
| `wifi_ducky_ap_start` / `wifi_ducky_ap_stop` | Captive AP + form server |
| `ping` / `status` | Bridge health check |

### Events the bridge emits

| Event | Source |
|---|---|
| `wifi_seen`, `ble_seen` | Continuous scanners |
| `pkt` | Promiscuous capture |
| `wifi_connected`, `wifi_disconnected` | STA state changes |
| `http_response` | Reply to http_get/post |
| `host_seen`, `host_scan_done` | Net scanner |
| `channel` | RF spectrum sweep |
| `eapol_seen`, `handshake` | WPA collector |
| `ble_service`, `ble_char`, `ble_value` | GATT discovery / read |
| `ble_connected`, `ble_disconnected` | GATT central state |
| `wifi_ducky_form` | Captive AP form post |
| `pong`, `status` | Health responses |

---

## Part 9 — The 56 applications

Briefly, by category. Full screens and pixel-level UIs vary; what each does:

### SYSTEM (9)

| App | Purpose |
|---|---|
| About | Version, license, hardware, memory, credits |
| Files | Read-only file viewer for SD |
| File Manager | Browse, copy, move, delete files on SD |
| ELF Browser | List + launch sandboxed RISC-V ELFs (deferred for now) |
| Gamepad | BLE gamepad pairing |
| Bridge | Live console of C6 bridge events |
| MicroPython | MicroPython REPL (deferred) |
| System | Live stats: heap, PSRAM, uptime, task list |
| **C6 Flasher** | Pick a `.bin` from `/sd/ghost/` and reflash the C6 |

### TOOLS (5)

| App | Purpose |
|---|---|
| Notepad | Quick notes saved to `/sd/notes/` |
| Calculator | Standard + subnet calculator (CIDR / network / broadcast) |
| Clock | Time + date + stopwatch |
| Calendar | Month grid view |
| Etch | Touch drawing canvas |

### INTEL (7)

| App | Purpose |
|---|---|
| Terminal | Bridge-driven shell-style command interface |
| Gemini Log | Logs of an AI chat session (uses HTTP via bridge) |
| Ref Med | Medical reference (offline DB) |
| Ref Surv | Wilderness survival reference (offline DB) |
| Baseball | Live MLB scores (HTTP via bridge) |
| Trails | POI / heat-map for hiking trails (offline DB + GPS) |
| SSH | SSH client (HTTP gateway via bridge — partial) |

### GAMES (7)

| App | Purpose |
|---|---|
| Snake, Pac-Man, Galaga | Arcade classics with high-score persistence |
| Chess | Local two-player, optional AI |
| Doom | id Software open-source Doom port (deferred polish) |
| SimCity | Tiny city builder |
| Retro ELF | Launcher for RISC-V mini-game ELFs |

### MEDIA (2)

| App | Purpose |
|---|---|
| Audio Player | WAV/MP3 playback from SD via NS4168 |
| Audio Recorder | PDM mic capture, save to SD |

### COMMS (6)

| App | Purpose |
|---|---|
| GPS | Live coords, sats, speed, raw NMEA |
| WiFi | Scan, connect, disconnect (via C6) |
| Bluetooth | Scan + pairing controls (via C6) |
| LoRa Voice | Push-to-talk over SX1262 FSK voice mode + Codec2 |
| Mesh Messenger | Meshtastic LongFast text chat |
| Voice Terminal | Voice-driven command interface (TTS pending) |

### CYBER (14)

| App | Purpose |
|---|---|
| Wardrive | Always-on capture log → SQLite session DB |
| BT Radar | BLE devices on the air with RSSI rings |
| Pkt Sniffer | Live promiscuous capture to screen |
| Beacon Spotter | Detect known/anomalous WiFi beacons |
| Net Scanner | ARP-sweep of the connected subnet |
| Hash Tool | MD5/SHA-1/SHA-256 of files or text |
| BLE GATT | Connect to a BLE device, walk services, read chars |
| WPA HS | Collect 4-way handshakes, save hccapx |
| RF Spectrum | Channel utilization sweep across 2.4 GHz |
| Probe Intel | Track WiFi probe requests with directory matching |
| Pkt Analysis | Open a saved capture, analyze offline |
| BLE Ducky | BLE HID injection scripts |
| USB Ducky | USB HID injection (TinyUSB) |
| WiFi Ducky | Captive AP — phone connects, types script in browser |

---

## Part 10 — The radio slot architecture

The wireless module slot on the CrowPanel is one of its most distinctive features. It accepts five different ELECROW carriers, each a different radio:

| Module | Radio | Use case |
|---|---|---|
| SX1262 | LoRa 868/915 MHz | Long-range mesh + voice (Meshtastic-compat) |
| nRF24 | 2.4 GHz proprietary | Toy/peripheral protocols, MouseJack-class |
| ESP32-H2 | Thread/Zigbee/Matter | Smart-home enumeration, Matter observation |
| ESP32-C6 (slot) | WiFi 6 / BLE / 802.15.4 | Second Ghost Engine instance |
| Wi-Fi HaLow | 802.11ah sub-GHz | Long-range Wi-Fi |

### Auto-detection

The `pm_radio` component probes the slot at boot. SX1262 and nRF24 share the same SPI bus and control pins, so distinguishing them uses chip-specific signature reads:

- **SX1262:** pulse NRST, send GET_STATUS (0xC0). Expected response has a non-zero CHIPMODE field.
- **nRF24L01+:** read STATUS register, write CONFIG=0x0B, read back. Real chip writes will stick.
- If neither responds, the slot is empty and `pm_radio_kind()` returns `PM_RADIO_NONE`.
- H2, slot-C6, and HaLow are full MCUs with their own firmware; they can't be auto-detected the same way and are user-declared via `pm_radio_init_as()`.

### Backend dispatch

After detection, all radio operations route through `pm_radio.c`'s dispatch table. Apps call `pm_radio_tx`, `pm_radio_set_rx_cb`, `pm_radio_take`/`give`. The dispatcher routes to either the LoRa backend (in `pm_lora.cpp`) or the nRF24 backend (in `pm_radio_nrf24.cpp`). Apps don't know which backend is active.

Mesh and voice apps explicitly check `pm_radio_kind() == PM_RADIO_SX1262` before initialization — they're LoRa-specific. A future nRF24 sniffer app would check for `PM_RADIO_NRF24`.

> **Hardware module mismatch:** The Waveshare Core1262-HF SX1262 module has a 2×7 DIP footprint that does NOT fit ELECROW's proprietary slot. To use LoRa with this firmware, buy ELECROW's $6.55 SX1262 carrier. The radio itself is the same SX1262 chip; only the carrier PCB shape differs.

---

## Part 11 — Storage: SD, SQLite, and NoSQL

### SD card

MicroSD on SDIO 1-bit. Mounts at `/sd`. The `pm_hal` component handles mounting; if the card is missing or corrupt, mounting fails and the OS continues without it (most apps degrade gracefully).

Standard directories the firmware uses or expects:

```
/sd/sessions/                wardrive session DBs (one per boot)
/sd/notes/                   notepad files
/sd/recordings/              audio recorder output
/sd/captures/                packet captures (offline pkt analysis)
/sd/ghost/                   C6 firmware blobs (.bin) for reflashing
/sd/elf/                     RISC-V ELF modules for ELF browser
/sd/.config/                 NoSQL store (key-value JSON)
```

### `pm_sqlite` — wardrive session DB

Wraps the `siara-cc/sqlite3` managed component. Each wardrive session writes to a fresh database at `/sd/sessions/session_<timestamp>.db`. The schema:

```sql
wifi_seen   (bssid PK, ssid, last_rssi, last_channel, enc, count, last_ts)
ble_seen    (mac PK, name, last_rssi, last_ts, count)
probes      (mac, ssid, rssi, ts)
packets     (id PK, frame_type, src, dst, bssid, rssi, channel, ts)
gps_track   (id PK, ts, lat, lng, alt_m, speed_mps, sats)
metadata    (key TEXT PK, value TEXT)
```

If SQLite init fails (e.g. SD missing or full), the wardrive falls back to a per-session CSV file in the same directory. The CSV-fallback flag is exposed in the wardrive UI.

### `pm_nosql` — simple key-value store

Lightweight JSON-backed store for app preferences and small persistent state. Each "namespace" is a file under `/sd/.config/<namespace>.json`. Used for things like notepad's last-opened file, audio player's playlist position, hash tool's recent inputs.

Three operations:

```c
pm_nosql_get(namespace, key, default_value);
pm_nosql_set(namespace, key, value);
pm_nosql_delete(namespace, key);
```

### NVS — boot-time settings

ESP-IDF's built-in non-volatile storage on the chip's flash (separate from SD). Used for things that need to survive an SD card removal: WiFi credentials, brightness, theme. Capped to a few KB.

---

## Part 12 — UI: LVGL and the Pisces UI kit

### LVGL

LVGL (Light and Versatile Graphics Library) is the embedded GUI framework. It handles widget primitives (labels, buttons, lists, bars, animations), input dispatch (touch coordinates → widget events), and the redraw cycle. Pisces Moon uses LVGL v9.x.

LVGL doesn't impose a particular look — every app could look completely different. To prevent that, Pisces Moon adds a thin UI kit on top.

### The Pisces UI kit

The `pm_ui` component is a small library of widget builders that produce themed, consistent widgets. The palette is fixed:

```
PM_C_BG          0x0A1828   deep sea (background)
PM_C_BG_2        0x122B45   card background
PM_C_BG_3        0x1A3A5C   raised surface
PM_C_FG          0xE6F0FA   moonlight (text)
PM_C_FG_DIM      0x8FA8C2   secondary text
PM_C_ACCENT      0x4FD1C5   teal moon (primary)
PM_C_ACCENT_2    0xB4A0FF   pisces purple (secondary)
PM_C_OK          0x4ADE80   green
PM_C_WARN        0xFBBF24   amber
PM_C_ERR         0xF87171   red
```

Builders the kit exposes:

- `pm_ui_screen()` — fresh themed screen with column flex layout
- `pm_ui_titlebar(parent, title, back_cb, back_user)` — back button + title
- `pm_ui_card(parent)` — bordered, padded container
- `pm_ui_button(parent, label, cb, user)` — themed button
- `pm_ui_chip(parent, text, color)` — small status pill
- `pm_ui_kv_row(parent, key, initial)` — "key: value" row, returns value label
- `pm_ui_status_dot(parent, color)` — circle for state indication
- `pm_ui_list(parent)` — themed scrollable list
- `pm_ui_meter_bar(parent, min, max)` — horizontal bar
- `pm_ui_keypad(parent, layout, cb, user)` — calc-style numeric pad
- `pm_ui_log_panel(parent)` — append-only scrollback
- `pm_ui_default_screen(title, status)` — fallback screen for unfinished apps

### App screen pattern

Most apps follow this shape in their `_build_screen()`:

```c
static void _build_screen(void) {
    s_screen = pm_ui_screen();
    pm_ui_titlebar(s_screen, "MY APP", NULL, NULL);
    lv_obj_t* card = pm_ui_card(s_screen);
    s_value_label = pm_ui_kv_row(card, "Status", "...");
    pm_ui_button(s_screen, "Action", _action_cb, NULL);
}
```

And in `tick()` they update the labels: `lv_label_set_text(s_value_label, ...)`. Four reference apps demonstrate the full surface: clock (kv rows + status), calculator (keypad), wifi (list + buttons), wardrive (chips + numeric stat grid).

> **Default screen pattern:** 44 of 49 apps currently use `pm_ui_default_screen()` as a placeholder. They boot, are navigable, and show a status string — but their custom UIs aren't done yet. The four showcase apps establish the recipe; the rest are mechanical work upgrading one at a time.

---

## Part 13 — Build, install, flash

Detailed coverage is in the companion document, `PiscesMoon_P4_BringUp_Guide.docx`. Here is the architectural overview.

### Toolchain

ESP-IDF v5.4.x. Components:

- `xtensa-esp-elf-gcc` (legacy chips) and `riscv32-esp-elf-gcc` (P4 / C6) — the C/C++ compilers
- Python 3 + esp-idf Python tools — drive the build, flash, monitor cycle
- CMake — the underlying build orchestration
- esptool.py — uploads binaries to the chip over USB-serial

### Two ways to install

**VS Code path (recommended):**

- Install the "ESP-IDF" extension by Espressif Systems
- Run "ESP-IDF: Configure ESP-IDF Extension" → EXPRESS install → v5.4.x
- Open the project folder, set target to `esp32p4`, pick the serial port
- Run "ESP-IDF: Build, Flash and start a Monitor" — done

**CLI path (for those who prefer terminal):**

```bash
git clone --recursive --branch v5.4.2 https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32p4
. ~/esp/esp-idf/export.sh
cd /path/to/pisces-moon-p4
idf.py set-target esp32p4
idf.py -p /dev/ttyUSB0 flash monitor
```

### Two firmware trees

`pisces-moon-p4/` and `pisces-moon-c6/` are independent ESP-IDF projects. They build separately. They flash separately (when the C6 SDIO flasher is brought up). They're meant to be released together as one OS but treated as two firmwares operationally.

---

## Part 14 — C6 firmware: how it gets onto the chip

### The setup

The CrowPanel Advanced 7" exposes a dedicated **UART1 connector** on the top-right of the board (silkscreen labels: `RX1 / TX1 / 3V3 / GND`). This connector goes **directly to the C6's console UART** — it's how ELECROW expects you to flash custom firmware to the C6.

Discovery sequence in this project: we initially planned a P4-mediated SDIO flash path because we thought the C6 console wasn't exposed. Reading the haraldkreuzer review more carefully (and re-reading line 79 of our own original handoff document) revealed that the external UART1 connector is the documented C6 programming path. The SDIO flasher work stays in the tree as future-board infrastructure (for hypothetical boards that don't expose the C6 externally), but it's no longer the primary flash path for this board.

### Primary flash path: direct UART1 + esptool

Equipment:
- USB-to-TTL adapter (CP2102 or CH340-based, $3-5)
- Four jumper wires

Wiring (USB-TTL → CrowPanel UART1 connector):
```
USB-TTL TX  → RX1
USB-TTL RX  → TX1
USB-TTL 3V3 → 3V3
USB-TTL GND → GND
```

Procedure:
1. Build the C6 firmware: `cd pisces-moon-c6 && idf.py set-target esp32c6 && idf.py build`
2. Plug the USB-TTL adapter into your computer; identify the serial port
3. Flash: `idf.py -p /dev/ttyUSB0 flash` (or VS Code: "ESP-IDF: Flash your project")
4. The adapter's DTR/RTS lines drive the C6's BOOT/EN automatically; the C6 enters bootloader mode and accepts the firmware

If auto flash-mode fails (rare but possible depending on adapter), the C6's BOOT pad would need to be held low manually during reset — a soldering step the alpha tries to avoid. In practice the adapter handles this cleanly on the C6.

### Future path: SDIO-mediated flash from the P4

For hypothetical boards (or future revisions) that don't expose the C6 console externally, `pm_c6_programmer` implements the ESP serial bootloader protocol over the internal SDIO bus that already connects the P4 and C6 for normal coprocessor operations.

The protocol layer is complete: SLIP framing, command structure, MD5 verify hook, FLASH_BEGIN/DATA/END streaming. The SDIO transport (`_sdio_send` / `_sdio_recv`) is stubbed pending boards that need it.

The C6 Flasher app in the SYSTEM category exposes both paths: it can either (a) walk the user through the UART1 + esptool procedure with on-screen instructions, or (b) on supported boards, drive the SDIO transport directly.

### Implementation status

| Component | Status |
|---|---|
| Direct UART1 + esptool path | Available now (no software needed; uses esptool directly) |
| `pm_c6_programmer` protocol layer | Complete (~430 LOC) |
| `pm_c6_programmer` SDIO transport | Stubbed, future-board work |
| C6 Flasher app UI | Built; targets SDIO path; needs UART1-instructions mode added |

> **Bottom line:** Flash custom C6 firmware via the external UART1 connector with a USB-TTL adapter. It's the supported, documented, and immediate path. The SDIO work is good architecture for the future but isn't blocking anything today.

---

## Part 15 — The modular peer model

Pisces Moon is not "firmware for a specific board." It's a **modular operating system for ESP32 hardware** with a peer-registry spine that auto-detects optional hardware and presents a uniform capability surface to applications.

### The two-tier identity

**Permanent fixtures** are things the OS knows are always present on whatever board it's running on:
- ESP32-P4 (the host application processor)
- ESP32-C6 (always-on radio coprocessor — runs Ghost Engine firmware)
- BN-180 GPS (or whatever GPS the chassis provides on a dedicated UART)
- Display + touch + audio (chassis-defined)
- SD storage

**Modular peers** are things that may or may not be attached:
- PN532 NFC reader (on the C6's external UART1 connector)
- T-Beam Supreme S3 (on the P4's UART2, secondary radios)
- Wireless slot module (SX1262, nRF24, ESP32-H2, etc.)
- CSI camera module
- BLE HID peripherals (paired gamepads, keyboards)

The OS never crashes because a modular peer is missing. Apps that need a missing capability show "feature unavailable" and return cleanly.

### The peer registry

`pm_peer` is a single source of truth: "what capabilities does the currently-attached hardware offer?" Each peer announces itself at boot or hot-plug time with a kind, a name, and a list of capability strings:

```c
static const char* const TBEAM_CAPS[] = {
    "wifi_scan", "wifi_capture", "ble_scan", "ble_gatt",
    "lora_tx", "lora_rx", "lora_mesh", NULL,
};
pm_peer_announce(PM_PEER_KIND_TBEAM_S3, "T-Beam Supreme S3", TBEAM_CAPS);
```

Apps query by capability, optionally specifying a role:

```c
pm_peer_t* primary = pm_peer_find("wifi_scan", PM_PEER_ROLE_PRIMARY);
pm_peer_t* parallel = pm_peer_find("wifi_scan", PM_PEER_ROLE_SECONDARY);

if (primary) pm_peer_call(primary, "scan", NULL);
if (parallel) pm_peer_call(parallel, "scan", NULL);
```

### Roles

| Role | Meaning |
|---|---|
| `PRIMARY` | The canonical, always-on owner of the capability. Apps that want "the wardrive log" use this. |
| `SECONDARY` | A non-primary provider. Apps that want to "scan WHILE wardrive runs" use this. |
| `ANY` | First match; usually the primary, fallback to secondary if primary absent. |
| `EXCLUSIVE` | Like PRIMARY but registers a hold; other callers blocked until released. |

### Routing rules (CrowPanel Advanced 7")

| Capability | PRIMARY | SECONDARY | Fallback |
|---|---|---|---|
| WiFi scan / capture | C6 (always-on Ghost Engine) | T-Beam (if connected) | C6 |
| BLE scan / GATT | C6 (always-on) | T-Beam (if connected) | C6 |
| LoRa TX / RX | wireless slot SX1262 (if installed) | T-Beam LoRa (if no slot module) | unavailable |
| NFC read / write | PN532 via C6 UART1 | — | unavailable |
| GPS fix | BN-180 P4-direct | — | (always present) |
| Camera | CSI module | — | unavailable |
| Input gamepad | paired BLE HID | — | on-screen virtual |
| Input keyboard | paired BLE HID | — | on-screen virtual |

Importantly: the wireless slot module **does not** suppress T-Beam entirely. It owns the LoRa class (because SX1262 in the slot is the canonical LoRa primary), but T-Beam's WiFi/BLE remain available as SECONDARY peers. This is per-capability routing, not strict suppression.

### The Kode Dot rationale

When Kode Dot ships and exposes its own expansion modules, Pisces Moon ports cleanly because the OS doesn't assume any specific peripheral set. Each Kode Dot module becomes another peer kind; capabilities register; apps work unchanged. This is why Phase 15 was scoped so aggressively — once the modular peer model is in place, every future board is mostly just a BSP + peer-driver port.

---

## Part 16 — The input layer

The CrowPanel Advanced 7" has no physical keyboard and no trackball — only the capacitive touchscreen. Several apps need text input (notepad, terminal, SSH, voice_terminal, calculator-in-expression-mode) or directional input (games). The input layer solves this with three sources that all converge into one dispatcher.

### Three input sources, one event stream

**On-screen QWERTY keyboard** (`pm_ui_keyboard_t`)
- Wraps LVGL's built-in `lv_keyboard` widget with Pisces theming
- Apps call `pm_ui_keyboard_create(parent)` and `pm_ui_keyboard_attach(kb, textarea)` to bind it to an LVGL textarea
- Each keystroke is also posted to the pm_input dispatcher so non-LVGL consumers can subscribe

**On-screen virtual gamepad** (`pm_ui_gamepad_t`)
- Eight tap zones laid out like a Game Boy: 4-way D-pad on the left, A/B/X/Y diamond on the right, Start/Select strip in the middle
- Each tap posts pm_input events with `source = PM_INPUT_SRC_VIRTUAL_GAMEPAD`
- Games show it as an overlay; press = `down=true`, release = `down=false`, so games can implement hold-to-walk

**Paired Bluetooth HID device** (via the C6)
- `ghost_hid_host.c` on the C6 implements BLE central role for HID devices
- 8BitDo Zero 2 in BLE-HID mode (Start+Y power-on combo), generic BLE keyboards, anything advertising HID service 0x1812
- HID input reports are parsed on the C6, diff'd against last state, and emitted as bridge events:
  - `{"event":"gamepad_event","btn":N,"dpad":bitmask,"down":bool}`
  - `{"event":"keyboard_event","code":N,"down":bool,"mods":N}`
- The P4-side bridge dispatcher catches these and posts to pm_input

### Unified event model

All three sources funnel into `pm_input`:

```c
typedef struct {
    pm_input_kind_t   kind;     // KEY, BUTTON, DPAD
    pm_input_source_t source;   // VIRTUAL_KEYBOARD, VIRTUAL_GAMEPAD, BT_GAMEPAD, BT_KEYBOARD
    uint32_t          code;     // ASCII keycode, PM_BTN_*, or PM_DPAD_* bitmask
    bool              down;     // press vs. release
    uint32_t          timestamp;
} pm_input_event_t;

pm_input_post(&event);     // virtual widgets and bridge handler use this
pm_input_subscribe(cb, user);  // apps subscribe with handlers
```

Apps don't care where input came from. Snake's directional handler treats a D-pad UP from a real 8BitDo gamepad identically to a tap on the on-screen UP zone. Notepad treats a "B" keystroke from a paired BLE keyboard identically to a "B" tap on the on-screen keyboard.

### Graceful fallback

If no BLE HID device is paired, on-screen virtual widgets remain available. If a peripheral disconnects mid-session, apps don't crash; the virtual widget overlay is always there as a fallback.

This is the input-layer equivalent of the peer registry's "no NFC plugged in? apps degrade cleanly" philosophy. Modularity all the way down.

---

## Part 17 — Phase 15 additions: NFC, camera, T-Beam

The Phase 15 additions land seven new apps and three new peer types:

### NFC apps (4 in CYBER)

The C6 owns the PN532 driver (`ghost_nfc.c`), so the P4-side apps subscribe to bridge events and treat NFC as just another peer-routed capability.

- **NFC Reader** — tap a tag, see UID + type + full data dump
- **NFC Clone** — read source tag, save to `/sd/nfc/`, write to blank
- **NFC Emulate** — emulate a stored tag from SD (with in-app legal disclaimer about emulating only tags you own)
- **Amiibo** — NTAG215 backup/restore for legit Amiibo workflows

PN532 jumpers must be set to HSU mode (both DIP switches at 0,0); plugs into the C6's external UART1 connector.

### Camera apps (1 in MEDIA, 1 in TOOLS)

The CrowPanel has a dedicated MIPI-CSI ribbon connector (CIS-CAM) that physically accepts a CSI camera module. The ESP32-P4 has native MIPI-CSI hardware.

- **Camera** (MEDIA) — viewfinder + snapshot to `/sd/photos/`
- **Camera QR scanner** (TOOLS) — barcode/QR decode via `quirc` library

Sensor driver integration uses Espressif's `esp_video` managed component, which abstracts over common sensors (SC2336, OV2640, etc.). Phase 15 ships the skeleton; sensor probe + driver hookup pending bench validation.

### T-Beam Supreme S3 peer (1 new app + restructured comms)

When the T-Beam is wired to the CrowPanel's 2×12 GPIO header (RX=IO25, TX=IO27, GND), it offers SECONDARY WiFi/BLE and FALLBACK LoRa. The bridge protocol mirrors the C6's: one JSON object per line over UART at 921600 baud.

The matching T-Beam firmware (`pisces-moon-tbeam-s3/`) is a separate sub-project, deferred to Phase 16. Phase 15 ships:
- The P4-side `pm_tbeam` component (protocol + peer announcer)
- Bridge command/event definitions in `pm_tbeam.h`
- The Secondary Scan CYBER app, which demonstrates parallel-radio capability (T-Beam scans WiFi while C6 wardrives in background)

The T-Beam's own GPS is intentionally disabled by policy — BN-180 on the P4 remains the canonical fix source. Two GPSes adds power cost with no real benefit, and avoids fix-source contention.

### App count: 49 → 56

| Category | Phase 13 | Phase 15 | Phase 15 additions |
|---|---:|---:|---|
| SYSTEM | 9 | 9 | — |
| TOOLS | 5 | 6 | Camera QR scanner |
| INTEL | 7 | 7 | — |
| GAMES | 7 | 7 | — |
| MEDIA | 2 | 3 | Camera |
| COMMS | 6 | 6 | — |
| CYBER | 14 | 19 | NFC Reader, Clone, Emulate, Amiibo, Secondary Scan |
| **Total** | **49** | **56** | **+7** |

---



## Part 18 — Licensing and contribution

### AGPL-3.0-or-later

Pisces Moon OS is licensed under the GNU Affero General Public License version 3 or later. The full text is in `LICENSE`.

In short:

- Free to use, modify, distribute, sell
- Modifications must be released under AGPL too
- If you run a network service from a modified version, you must publish your modifications. This is the "network use" clause that distinguishes AGPL from GPL.
- Every source file carries an `SPDX-License-Identifier` comment

### Why AGPL

Pisces Moon is meant to be a public good and an architectural statement. The dedicated-radio-MCU design, the always-on Ghost Engine, the slot-pluggable radios — these are deliberate design choices. AGPL ensures that derivative works keep them open. If someone takes Pisces Moon, modifies it, and runs a service from the modification, they have to publish their changes back. Plain GPL doesn't require this for network-only deployments; AGPL does. For a project whose distinctive features are architectural rather than functional, AGPL is the right choice.

### Contributor License Agreement

Pull requests against either repository require agreeing to the CLA in `CLA.md`. The CLA grants Eric Becker / Fluid Fortune a license to redistribute your contribution under the project's license. It does **NOT** transfer copyright — you keep that. It does **NOT** prevent you from using your contribution elsewhere. It exists so the project can re-license under future AGPL-compatible licenses without needing to chase down every contributor for permission.

Signing the CLA is implicit when you open a pull request: include the line "I have read the CLA at CLA.md and agree to its terms" in your PR description.

### Header convention

Every source file in both trees carries this block:

```c
// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com
```

For Markdown, HTML, and YAML files the same content is wrapped in `<!-- -->` or `#` comment syntax. The header pass is automated; no contributor needs to remember to add it manually.

### Where to find the project

- **Repository:** [github.com/FluidFortune/PiscesMoon](https://github.com/FluidFortune/PiscesMoon)
- **Web:** [fluidfortune.com](https://fluidfortune.com)
- **Issues, PRs, discussion:** GitHub
- **Live demo of older S3 version:** also at fluidfortune.com

---

This is the architecture reference. The companion bring-up guide covers the operational side: install ESP-IDF, build, flash, and what success looks like.

*— Pisces Moon OS / Fluid Fortune / [fluidfortune.com](https://fluidfortune.com) / AGPL-3.0-or-later*
