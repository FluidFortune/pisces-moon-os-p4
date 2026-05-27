<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md

fluidfortune.com
-->

# Pisces Moon OS — Installation Guide

**Audience:** Someone comfortable in VS Code, new to ESP-IDF, holding
a CrowPanel Advanced 7" board.

**Time:** ~2 hours first time (mostly toolchain download). ~30 seconds
per build/flash cycle after that.

**What you'll have at the end:** Pisces Moon launcher running on the
screen, GPS lock, 56 apps loadable, ready to plug in modular peripherals.

---

## Part 0 — What you need

### Hardware
- **ELECROW CrowPanel Advanced 7"** ESP32-P4 HMI (DHE04107D V1.0)
- **USB-C cable** — data-capable, not charge-only
- **Computer** running Windows 10/11, macOS 11+, or modern Linux
- **~8 GB free disk space**
- Internet connection (~4 GB toolchain download)

### Optional modular peers (the OS works without them; works better with them)
- **PN532 NFC reader** (red v3 board, ~$8) — for the 4 NFC apps
- **T-Beam Supreme S3** — for secondary WiFi/BLE and LoRa
- **CSI camera module** — for camera + QR scanner apps
- **8BitDo Zero 2 gamepad** (or any BLE HID gamepad) — for game input
- **Wireless slot module** (ELECROW SX1262 LoRa carrier $6.55, or nRF24)

### Software (we'll install these)
- Visual Studio Code
- ESP-IDF v5.4.x extension
- USB-to-serial driver (Windows only, usually automatic)

---

## Part 1 — Get the source

You have the bundle as `pisces-moon-alpha-1.2.0.zip`. Unzip it
somewhere with a clean path.

> **Path warning:** Don't put the project inside a folder whose name has
> spaces, parentheses, or special characters. Don't put it in OneDrive,
> iCloud, or Dropbox — those can interfere with file locking during
> builds. Recommended: `~/Documents/PiscesMoon/`.

After unzipping you'll have two folders:

```
PiscesMoon/
├── pisces-moon-p4/       ← P4 firmware (the main one)
└── pisces-moon-c6/       ← C6 firmware (the radio coprocessor)
```

These are **two separate ESP-IDF projects**. They build separately.
They flash separately. They communicate over an internal UART at
runtime. Most of the time you'll only touch the P4 tree.

---

## Part 2 — Install VS Code + ESP-IDF

### 2.1 Install VS Code
Download from [code.visualstudio.com](https://code.visualstudio.com).
Run the installer, accept defaults.

### 2.2 Install the ESP-IDF extension
Open VS Code. Press **Ctrl+Shift+X** (Cmd+Shift+X on Mac). Search for
`espressif idf`. Install "ESP-IDF" by Espressif Systems.

### 2.3 Configure ESP-IDF
After install, a setup screen appears automatically. If it doesn't,
press **Ctrl+Shift+P** and run `ESP-IDF: Configure ESP-IDF Extension`.

Choose **EXPRESS** install. Settings:

- **Download server:** Espressif download server (use GitHub if Espressif
  is slow)
- **ESP-IDF version:** highest **v5.4.x** available. NOT v5.5+.
- **Other fields:** leave at defaults

Click **Install**. This downloads ~4 GB and takes 15–60 minutes. You're
downloading the RISC-V C compiler (for P4) plus the Xtensa C compiler
(for C6), Python tools, and the ESP-IDF framework itself.

When it finishes: "All settings have been configured."

### 2.4 (Windows only) USB-to-serial driver
Plug the CrowPanel into your computer using the USB-C port labeled
**PWR / 382.0** near the bottom of the board (NOT the other USB-C).

Open Device Manager. Look under "Ports (COM & LPT)" for something like
"USB-SERIAL CH340 (COM5)". If you see it, you're done.

If you don't see it or see a yellow warning: download the CH340 driver
from `https://www.wch-ic.com/downloads/CH341SER_EXE.html`, run the
installer, replug the board.

Mac and Linux: skip this section, drivers are built in.

---

## Part 3 — Build the P4 firmware

### 3.1 Open the project
**File → Open Folder** → navigate to `pisces-moon-p4` (the inner folder,
not the parent). Click Open.

If prompted, click "Yes, I trust the authors."

You should see in the Explorer panel: `components/`, `main/`,
`CMakeLists.txt`, `partitions.csv`, etc.

### 3.2 Set the target chip
**Ctrl+Shift+P** → `ESP-IDF: Set Espressif Device Target` → pick **esp32p4**.

If asked for a board: pick "Custom board (esp32p4)" or whatever's
closest. Our project settings override the choice.

### 3.3 Pick the serial port
Look at VS Code's bottom toolbar. Find the icon shaped like a plug, or
the label showing the current port. Click it. Pick:

- **Windows:** the highest-numbered new COM port (e.g. COM5)
- **Mac:** `/dev/cu.usbserial-XXXX` (try this one first if multiple)
- **Linux:** `/dev/ttyUSB0` or `/dev/ttyACM0`

> **Linux note:** if you get permission errors later, run this once and
> log out/in: `sudo usermod -a -G dialout $USER` (Ubuntu/Debian) or
> `-G uucp` (Arch).

### 3.4 First build
**Ctrl+Shift+P** → `ESP-IDF: Build your project`.

A terminal opens showing build output. Watch for RED text (errors);
yellow text (warnings) is fine. **The first build pulls 4 GB of managed
components and toolchain — takes 5–10 minutes**. Subsequent builds run
in ~30 seconds.

Success looks like:
```
Project build complete. To flash, run:
   idf.py flash
Build complete (0:08:23)
```

Common first-build issues:

- **"Component not found"** → the build is downloading components, wait
  it out. If it persists: check internet connection.
- **"Set IDF_PATH"** → rerun `ESP-IDF: Configure ESP-IDF Extension`.
- **A specific file error** → copy the full error and send it; don't
  edit C unless you want to learn it.

---

## Part 4 — Flash the P4

### 4.1 Plug in
Use the USB-C port labeled **PWR / 382.0** near the bottom of the board.

### 4.2 Build, flash, and monitor
**Ctrl+Shift+P** → `ESP-IDF: Build, Flash and start a Monitor`.

The board will be flashed (uploads happen automatically; if it fails
with "Failed to connect," hold the **BOOT** button, briefly press
**RESET**, release **BOOT**, then re-run). After flash, a serial
monitor opens showing the boot log.

### 4.3 What success looks like

The boot log should show roughly:
```
I (350) PM_HAL: SPI Treaty bus ready
I (450) PM_BSP: GT911 touch detected at 0x5D
I (550) PM_BSP: BSP ready: 1024x600 MIPI-DSI + GT911
I (570) PM_RADIO: Probing wireless module slot...
I (700) PM_RADIO: -> none (slot empty)
I (720) PM_PEER: Modular peer registry initialized
I (730) PM_PEER: C6 ghost engine — permanent — registered
I (740) PM_PEER: BN-180 GPS — permanent — registered
I (750) PM_PEER: Probing PN532 NFC on C6 UART1...
I (1100) PM_PEER: PN532 not detected (modular — that's fine)
I (1110) PM_PEER: Probing T-Beam Supreme S3 on UART2...
I (4110) PM_TBEAM: T-Beam not present (no response within 3000ms)
I (4120) PM_LAUNCHER: launcher init
I (4140) PM_APPS: all 56 apps registered (49 + 7 Phase 15 modular)
```

And the screen should show the launcher with 7 category tiles:
SYSTEM, TOOLS, INTEL, GAMES, MEDIA, COMMS, CYBER.

> **Screen stays black but log shows BSP ready and launcher ran?**
> The panel is on but backlight may not be lit. Try tapping the screen
> — if touch events show in the log, the system is fine and we have a
> backlight wiring issue to chase. Send the log.

> **Boot loops with "Guru Meditation" or "abort()"?**
> Copy everything from the panic line through the next "ESP-ROM" line
> and send it. The hex addresses map to specific code locations.

---

## Part 5 — Build and flash the C6 firmware

This step is **optional for first boot** — the C6 ships from ELECROW
with stock coprocessor firmware that handles basic WiFi/BLE. Pisces
Moon's Ghost Engine firmware replaces it with the continuous wardrive
+ NFC sensor functionality. Most apps work without it; the CYBER apps
need it.

### 5.1 Open the C6 tree in a SECOND VS Code window
**File → New Window**, then File → Open Folder → `pisces-moon-c6`.

### 5.2 Set target
**Ctrl+Shift+P** → `ESP-IDF: Set Espressif Device Target` → **esp32c6**.

### 5.3 Build
**Ctrl+Shift+P** → `ESP-IDF: Build your project`.

First build downloads C6-side components (~2 min after the P4 toolchain
is already installed).

### 5.4 Flash the C6
**This is where it gets specific to the CrowPanel.**

The C6 doesn't have its own USB-C connection. It's wired internally to
the P4. However, the board exposes a **dedicated UART1 connector**
(top-right, labeled `UART1` with pins `RX1 / TX1 / 3V3 / GND`) that
goes directly to the C6's programming UART.

You need a **USB-to-TTL adapter** (cheap, $3-5 from any electronics
supplier — search "CP2102" or "CH340 USB-TTL"). Wire it:

| USB-TTL | CrowPanel UART1 |
|---|---|
| TX | RX1 |
| RX | TX1 |
| 3V3 | 3V3 |
| GND | GND |

Plug the USB-TTL into your computer. In the **C6 VS Code window**, pick
that new port from the bottom toolbar.

To put the C6 in flash mode, you'll need the C6's BOOT and RESET pins
exposed. **The CrowPanel doesn't break these out externally** — this is
the open problem we've talked about. Options:

1. **Try without manual BOOT/RESET.** Some C6 boards auto-enter flash
   mode when esptool drives DTR/RTS. The USB-TTL adapter may handle this.
   Just try `ESP-IDF: Flash your project` and see.
2. **If that fails:** you need physical access to the C6 module's BOOT
   pad and reset line. This requires opening the board and soldering
   test wires, which we agreed isn't acceptable for the alpha.
3. **The future SDIO flasher path** (`pm_c6_programmer` in the P4 tree)
   is designed for exactly this case but isn't yet bring-up-verified.

For first bring-up, **leave the C6 on stock firmware**. The basic
features work. Phase 16 will close this gap.

---

## Part 6 — Plug in modular peers

Now the fun part. The OS auto-detects everything you plug in.

### NFC reader (PN532)
- Set the PN532 jumpers to **HSU mode** (both DIP switches at position 0,0)
- Connect to the **UART1 dedicated connector** (top-right of board)
- Wire: GND→GND, VCC→3V3, SDA→TX1, SCL→RX1
- Reboot the P4 (RESET button)
- Boot log should now show `PM_PEER: PN532 detected on C6 UART1 — registered`
- The 4 NFC apps in CYBER are now usable

### T-Beam Supreme S3
- Connect 3 wires to the CrowPanel's 2×12 GPIO header (left strip)
- Wire: T-Beam TXD → P4 IO25, T-Beam RXD → P4 IO27, GND → GND
- **T-Beam needs Pisces Moon Peer firmware** (see next section — currently deferred to Phase 16)
- Once running: boot log shows `PM_TBEAM: T-Beam connected`
- The Secondary Scan CYBER app becomes usable

### CSI camera
- Already plugged into the **CIS-CAM** ribbon connector
- Phase 15 ships the skeleton; real sensor driver is pending bench validation
- Camera apps load but viewfinder shows placeholder until driver is finished

### Wireless slot module
- Power the board off
- Slide the **Wireless module** switch to OFF position
- Plug in the module (ELECROW SX1262 carrier, nRF24 carrier, etc.)
- Slide the switch to ON
- Power the board back on
- Boot log: `PM_RADIO: -> SX1262 ready` (or whichever module)
- Mesh Messenger and LoRa Voice apps become functional

### BLE gamepad (8BitDo Zero 2)
- Open the **Gamepad** app under SYSTEM
- Put the gamepad in BLE-HID pairing mode (hold START+Y for ~3 seconds while powering on the 8BitDo)
- Tap **PAIR** in the app
- Once paired, the gamepad provides input to all games

---

## Part 7 — Daily workflow

Once you have a working setup:

**Each new firmware version I send you:**
1. Save the new zip somewhere (e.g. `PiscesMoon/zips/phase15.zip`)
2. Unzip it
3. In VS Code: File → Open Folder → new `pisces-moon-p4` folder
4. Run `ESP-IDF: Build, Flash and start a Monitor`
5. Wait — first build of a new version takes 5-10 min; later builds in
   that folder are ~30 sec

**The 4 commands you'll actually use:**
- `ESP-IDF: Build your project` — compile only
- `ESP-IDF: Flash your project` — upload (no rebuild)
- `ESP-IDF: Monitor your device` — open serial log
- `ESP-IDF: Build, Flash and start a Monitor` — all three (everyday)

**Serial monitor hotkeys:**
- `Ctrl+]` exit monitor
- `Ctrl+T` then `R` reset board
- `Ctrl+T` then `H` help

---

## Part 8 — When to ask for help

Send me:
- The exact error message (copy-paste, not paraphrase)
- Which step you were on
- The last 20-30 lines of log before the error
- Whether you've modified any source files

Don't worry about:
- Yellow warnings during build
- "Some components requested are not available" — they download automatically
- One unexpected reset on first flash — happens during USB enumeration

---

## Part 9 — What success looks like end-to-end

After completing this guide you should have:
- ESP-IDF v5.4.x installed in VS Code
- `pisces-moon-p4` building cleanly with one command
- The board flashing without manual BOOT/RESET
- Pisces Moon launcher visible with 7 category tiles
- Touch working — tap a tile, category opens
- GPS lock once outdoors with sky visibility
- 56 apps loadable (each opens, shows its UI, returns to launcher)
- Boot log shows the peer registry detecting whatever you plugged in

From there: every new zip is just unzip → open folder → flash. No more
toolchain setup. No more configuration.

---

## Reference cards

### Verified pin map

| Subsystem | Pins |
|---|---|
| MIPI-DSI | DATA0=IO40, DATA1=IO39, DATA2=IO36, DATA3=IO35, CLKN=IO37, CLKP=IO38 |
| Touch (GT911) | SCL=IO46, SDA=IO45, INT=IO42, RST=IO40, addr=0x5D |
| Backlight | BL_PWR=IO29, BL_EN=IO31 |
| Wireless slot SPI | CLK=IO8, MISO=IO7, MOSI=IO6 |
| SX1262 (slot) | NSS=IO10, BUSY=IO9, IRQ=IO53, NRST=IO54 |
| nRF24 (slot) | CS=IO10, IRQ=IO9, CE=IO53 |
| Audio NS4168 | LRCLK=IO21, BCLK=IO22, SDATA=IO23, AMP=IO30 |
| PDM mic | MCLK=IO24, SD=IO26 |
| SD (SDIO 1-bit) | CLK=IO43, CMD=IO44, D0=IO39 |
| GPS (BN-180) | RX=IO2, TX=IO3, baud=9600 |
| T-Beam UART2 | RX=IO25 (from T-Beam TX), TX=IO27 (to T-Beam RX) |
| PN532 NFC | C6 UART1 external connector, HSU @ 115200 baud |

### Peer routing rules

| Capability | Primary | Secondary | If neither |
|---|---|---|---|
| WiFi scan | C6 (always-on) | T-Beam | C6 |
| BLE scan | C6 (always-on) | T-Beam | C6 |
| LoRa | Slot SX1262 | T-Beam LoRa | unavailable |
| NFC | PN532 via C6 | — | unavailable |
| GPS | BN-180 P4-direct | — | (always present) |
| Camera | CSI module | — | unavailable |
| Input | BLE HID device | — | on-screen virtual |

---

*Pisces Moon OS · Fluid Fortune · [fluidfortune.com](https://fluidfortune.com) · AGPL-3.0-or-later*
