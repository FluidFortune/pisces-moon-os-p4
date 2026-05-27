<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md
fluidfortune.com
-->

# Pisces Moon OS — Phase 15 Changelog

**Phase:** 15 "Modular Peers"
**Date:** 2026-05-10
**Base:** v1.2.0-alpha (Phases 1–13 packaged, Phase 14 input layer integrated)

---

## Headline

Phase 15 reframes Pisces Moon OS as a **modular operating system for
ESP32 hardware** — not "firmware for the CrowPanel." The reframe is
architectural and identity-level, supported by a new peer-registry
component (`pm_peer`) that auto-detects optional modules at boot,
registers their capabilities, and lets apps query by capability rather
than by hardware.

The CrowPanel Advanced 7" is now described as the **reference chassis**.
Its permanent fixtures (P4, C6, BN-180 GPS, display, audio) provide a
baseline. Everything else — NFC reader, T-Beam Supreme S3, wireless
slot module, camera, BLE HID peripherals — is a **modular peer**,
detected at boot and gracefully absent if missing.

This is the framing that unblocks future hardware: when Kode Dot ships
(or any other modular ESP32 board), Pisces Moon ports trivially because
the OS no longer assumes a fixed peripheral set.

---

## What this phase adds

### Code (~3,000 LOC)

**`pm_peer` component** — modular peer registry. The spine of the new
architecture. Apps call `pm_peer_find(capability, role)` and get a peer
handle (or NULL); ops are dispatched via `pm_peer_call`. Roles include
PRIMARY (always-on canonical), SECONDARY (parallel ops), ANY, and
EXCLUSIVE. The C6 and BN-180 register unconditionally; PN532, T-Beam,
slot modules, and BLE HID peers register conditionally.

**`pm_nfc` P4-side component** — Receives PN532 events over the C6
bridge. Exposes the app-friendly API (`pm_nfc_subscribe`,
`pm_nfc_read_block`, `pm_nfc_write_block`). Does not talk to the PN532
directly — that's the C6's job (consistent with "C6 owns sensors").

**`ghost_nfc.c` on the C6** — PN532 HSU driver on the C6's UART1
connector. Detects tag presence, reads UID, supports MIFARE Classic and
NTAG block read/write. Emits `nfc_present`, `nfc_absent`, `nfc_seen`,
and `nfc_data` events over the bridge.

**`pm_camera` component** — CSI camera skeleton. Wraps Espressif's
`esp_video` managed component (sensor probe, frame buffer allocation,
viewfinder mode, snapshot to SD). Full sensor driver integration is
pending hardware validation.

**`pm_tbeam` P4-side component** — T-Beam Supreme S3 secondary radio
peer. UART2 bridge on IO25/IO27 at 921600 baud. Probe → ping → register
with peer registry as secondary WiFi/BLE provider, primary LoRa fallback
(when wireless slot is empty). The matching T-Beam firmware (Pisces
Moon Peer for T-Beam S3) is a separate sub-project deferred to Phase 16.

**Five new CYBER apps:**
- `pm_app_nfc_reader` — tap a tag, show UID + type + dump
- `pm_app_nfc_clone` — read source, save to `/sd/nfc/`, write to blank
- `pm_app_nfc_emulate` — emulate a stored tag (with legal disclaimer)
- `pm_app_amiibo` — NTAG215 read/write for Amiibo backup/restore
- `pm_app_secondary_scan` — T-Beam-routed WiFi/BLE scan that runs
  alongside the C6 wardrive (demonstrates parallel-radio capability)

**One new MEDIA app:** `pm_app_camera` (viewfinder + snapshot)

**One new TOOLS app:** `pm_app_camera_qr` (barcode/QR decode via camera
frames, uses `quirc` decoder)

### Apps: 49 → 56

| Category | Phase 13 | Phase 15 | Change |
|---|---:|---:|---|
| SYSTEM | 9 | 9 | — |
| TOOLS | 5 | 6 | +1 (Camera QR) |
| INTEL | 7 | 7 | — |
| GAMES | 7 | 7 | — |
| MEDIA | 2 | 3 | +1 (Camera) |
| COMMS | 6 | 6 | — |
| CYBER | 14 | 19 | +5 (4 NFC + Secondary Scan) |
| **Total** | **49** | **56** | **+7** |

### Documentation

- **README.md completely rewritten** to reframe Pisces Moon as a
  modular ESP32 OS. New sections on modularity, routing rules,
  capability mapping, and the reference chassis vs. modular peers
  distinction.
- New section in the architecture reference (Part 16) on the peer
  model and modularity philosophy.
- Routing rules table documented as canonical reference.

---

## Why this matters

### The Kode Dot insight

When Kode Dot refused to publish a pinout for their expansion modules,
it became clear their hardware is modular by design but their software
pretends it isn't. They have no peer-registry equivalent. Each app
hard-codes the peripherals it expects, which means each board variant
requires recompilation and re-flashing.

Pisces Moon takes the opposite stance. The peer registry decouples
apps from hardware. The same firmware runs on:
- A CrowPanel Advanced 7" with nothing attached except permanent fixtures
- The same board with NFC plugged into the C6's UART1
- The same board with a T-Beam wired to IO25/IO27
- The same board with a wireless slot module installed
- All of the above simultaneously
- A future Kode Dot port with whatever modules they expose

No conditional compilation. No board profiles. The OS auto-detects and
registers; apps query by capability.

### The C6 identity sharpens

The C6 was framed in earlier phases as "the always-on radio coprocessor."
Phase 15 broadens that. Now the C6 owns **all sensors that the P4 can't
directly access**: WiFi, BLE, the PN532 NFC reader plugged into its
UART1 connector. The phrase "Ghost Engine + sensorium" captures it.

This is consistent with the modular framing: the C6 is itself a peer
that offers many capabilities, and additional peers (PN532) can attach
to the C6 itself, multiplying its capability set without adding direct
P4-side hardware drivers.

### Strict-suppression vs. per-capability routing — resolved

Earlier sessions debated whether the wireless slot module should
"suppress T-Beam entirely" or only override "the radio class it
provides." Phase 15 lands on **per-capability routing**:

- Wireless slot with SX1262 installed → SX1262 owns LoRa, T-Beam LoRa
  becomes unused
- Wireless slot empty + T-Beam connected → T-Beam owns LoRa as fallback
- T-Beam connected regardless of slot → still offers SECONDARY
  WiFi/BLE for parallel ops

This matches Eric's stated intent ("T-Beam Supreme's WIFI/BLE serves as
backup/secondary") and gives the secondary-scan app a real use case.

---

## What's still pending

### Hardware bring-up
- Verify PN532 detection on C6 UART1 with actual hardware
- Verify T-Beam handshake protocol on bench
- Verify CSI camera sensor identification (likely SC2336)
- TCXO vs. XTAL setting on whichever SX1262 carrier is plugged in

### Software completion
- T-Beam firmware sub-project (`pisces-moon-tbeam-s3/`) — Phase 16
- Full `esp_video` sensor driver integration in `pm_camera`
- `quirc` decoder integration in `pm_app_camera_qr`
- NFC emulation card persistence (currently in-memory only)

### Per-app UI polish
- Several Phase 15 apps still use the default-screen template
- NFC reader and camera viewfinder have real UIs; cloning, emulate,
  Amiibo, secondary scan, and QR scanner ship with placeholder screens

---

## Migration notes

If you've been tracking the alpha:
- Apps that previously called `pm_c6_cmd_send_raw` directly should
  migrate to `pm_peer_call(peer, op, params)` once your code path is
  obviously a peer query (e.g., `pm_peer_find("wifi_scan", PM_PEER_ROLE_PRIMARY)`)
- The C6 Flasher app's marketing copy now correctly identifies the
  external UART1 connector as the supported flash path
- `pm_radio` remains the canonical interface for the wireless slot
  module; `pm_peer` sits above it and dispatches by capability

The PR is non-breaking. Existing C6-direct calls continue to work.

---

## Files

### Added
```
pisces-moon-p4/components/pm_peer/
pisces-moon-p4/components/pm_nfc/
pisces-moon-p4/components/pm_camera/
pisces-moon-p4/components/pm_tbeam/
pisces-moon-p4/components/pm_apps/cyber/pm_app_nfc_reader.{c,h}
pisces-moon-p4/components/pm_apps/cyber/pm_app_nfc_clone.{c,h}
pisces-moon-p4/components/pm_apps/cyber/pm_app_nfc_emulate.{c,h}
pisces-moon-p4/components/pm_apps/cyber/pm_app_amiibo.{c,h}
pisces-moon-p4/components/pm_apps/cyber/pm_app_secondary_scan.{c,h}
pisces-moon-p4/components/pm_apps/tools/pm_app_camera_qr.{c,h}
pisces-moon-p4/components/pm_apps/media/pm_app_camera.{c,h}
pisces-moon-c6/main/ghost_nfc.{c,h}
```

### Modified
```
pisces-moon-p4/main/main.c
pisces-moon-p4/main/pm_apps_register.c
pisces-moon-p4/main/CMakeLists.txt
pisces-moon-p4/README.md  (full rewrite)
pisces-moon-c6/main/ghost_bridge.c  (NFC command dispatch)
pisces-moon-c6/main/CMakeLists.txt  (add ghost_nfc.c)
```

---

## Credits

Eric Becker / Fluid Fortune — design, architecture, hardware integration,
modular framing.

fluidfortune.com
