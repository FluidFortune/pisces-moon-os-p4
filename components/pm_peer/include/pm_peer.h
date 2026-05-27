// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_peer.h — Capability-based peer registry
//
//  Pisces Moon OS is a MODULAR ESP32 OS. The board running it
//  has two permanent fixtures:
//    - ESP32-C6 (Ghost Engine: WiFi, BLE, sensor coprocessor)
//    - BN-180 GPS (P4-direct on UART1)
//
//  Everything else is OPTIONAL and AUTO-DETECTED at boot:
//    - PN532 NFC reader (on the C6's UART1 connector)
//    - Wireless module in the slot (SX1262 / nRF24 / H2 / etc.)
//    - T-Beam Supreme S3 (on the 2x12 header, secondary radios)
//    - CSI camera (on the CIS-CAM ribbon connector)
//    - 8BitDo / other BLE HID device (paired via C6)
//    - Cardputer ADV Pisces Moon module over I2C
//
//  Apps don't know or care what hardware is attached. They
//  ask the registry:
//
//    pm_peer_t* p = pm_peer_find("nfc_read", PM_PEER_ROLE_ANY);
//    if (p) {
//        pm_peer_call(p, "read_tag", params, ...);
//    } else {
//        // graceful "no NFC available" message
//    }
//
//  ── Roles ──
//
//  A capability can have multiple providers; apps can request
//  a specific role:
//
//    PRIMARY    — the always-on, continuously-running provider
//                  (e.g., C6 wardrive: never stops)
//    SECONDARY  — explicitly NOT the primary; used for parallel
//                  ops alongside primary (e.g., T-Beam scans
//                  while C6 wardrives in background)
//    ANY        — caller doesn't care; registry picks
//    EXCLUSIVE  — caller will hold the peer until release
//                  (registry blocks other callers until given)
//
//  ── Fallback ──
//
//  When a capability is requested but no peer offers it:
//    - find() returns NULL
//    - apps show "feature unavailable" rather than crashing
//    - this is the modular philosophy: present what's there,
//      hide what isn't
//
//  ── Detection ──
//
//  pm_peer_init_auto() runs at boot. It probes:
//    1. C6 bridge handshake (always expected to succeed —
//       this is a permanent fixture)
//    2. Wireless slot (SPI signature probe via pm_radio)
//    3. T-Beam on UART2 (sends "ping", waits for "pong")
//    4. NFC reader (C6 reports "nfc_present" event)
//    5. Camera (probes I2C address of SC2336 sensor)
//
//  Modules detected during boot are registered as peers with
//  their capability lists. Hot-plug detection happens via
//  bridge events.
// ============================================================

#ifndef PM_PEER_H
#define PM_PEER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Peer roles ───────────────────────────────────────────────
typedef enum {
    PM_PEER_ROLE_ANY        = 0,
    PM_PEER_ROLE_PRIMARY    = 1,    // always-on, never interrupt
    PM_PEER_ROLE_SECONDARY  = 2,    // available for parallel ops
    PM_PEER_ROLE_EXCLUSIVE  = 3,    // caller wants exclusive hold
} pm_peer_role_t;

// ── Peer kind (transport / hardware family) ──────────────────
typedef enum {
    PM_PEER_KIND_C6_GHOST   = 0,    // onboard ESP32-C6 (permanent)
    PM_PEER_KIND_TBEAM_S3   = 1,    // LilyGO T-Beam Supreme S3
    PM_PEER_KIND_SLOT_SX1262 = 2,   // wireless slot, SX1262
    PM_PEER_KIND_SLOT_NRF24  = 3,   // wireless slot, nRF24L01+
    PM_PEER_KIND_SLOT_H2     = 4,   // wireless slot, ESP32-H2
    PM_PEER_KIND_SLOT_C6     = 5,   // wireless slot, ESP32-C6
    PM_PEER_KIND_SLOT_HALOW  = 6,   // wireless slot, Wi-Fi HaLow
    PM_PEER_KIND_NFC_PN532   = 7,   // PN532 (lives behind C6 NFC events)
    PM_PEER_KIND_CAMERA_CSI  = 8,   // MIPI-CSI camera
    PM_PEER_KIND_BT_GAMEPAD  = 9,   // paired BLE HID gamepad
    PM_PEER_KIND_BT_KEYBOARD = 10,  // paired BLE HID keyboard
    PM_PEER_KIND_CARDPUTER_I2C = 11,// Cardputer ADV module over I2C
} pm_peer_kind_t;

// ── Capability names (strings used at runtime) ───────────────
//
// Apps reference these by string for forward-compat. New
// capabilities can be added without breaking existing apps.
//
//   "wifi_scan"        — passive WiFi network listing
//   "wifi_capture"     — promiscuous frame capture
//   "wifi_promisc"     — provider can enter promiscuous frame mode
//   "wifi_connect"     — STA connect to AP
//   "ble_scan"         — passive BLE advertising scan
//   "ble_gatt"         — BLE GATT central
//   "ble_hid_host"     — pair / read BLE HID device
//   "lora_tx"          — LoRa transmit
//   "lora_rx"          — LoRa receive
//   "lora_mesh"        — Meshtastic-format mesh
//   "lora_voice"       — FSK voice mode
//   "nrf24_sniff"      — 2.4 GHz proprietary sniff
//   "nrf24_tx"         — 2.4 GHz proprietary transmit
//   "nfc_read"         — read NFC tag
//   "nfc_write"        — write NFC tag
//   "nfc_emulate"      — card emulation
//   "camera_snapshot"  — capture single frame
//   "camera_stream"    — live viewfinder
//   "camera_barcode"   — barcode/QR decode
//   "http_get"         — proxy HTTP GET (C6 has IP stack, P4 doesn't)
//   "http_post"        — proxy HTTP POST
//   "gps_remote"       — companion GPS fix/status
//   "keyboard_hid"     — companion physical keyboard events

// ── The peer handle ──────────────────────────────────────────
typedef struct pm_peer_s pm_peer_t;

// Get a peer's kind and human-readable name.
pm_peer_kind_t pm_peer_kind(const pm_peer_t* p);
const char*    pm_peer_name(const pm_peer_t* p);

// ── Init ─────────────────────────────────────────────────────
//
// Run at boot AFTER pm_hal_init and bridge init. Probes for all
// optional modules. Returns the number of peers registered.
//
// The C6 Ghost Engine and BN-180 GPS are always registered as
// they are permanent fixtures; they're added even if probe fails
// (with a "degraded" flag so apps know).
int pm_peer_init_auto(void);

// ── Lookup ───────────────────────────────────────────────────
//
// Find the best peer providing a given capability. Returns NULL
// if no peer offers it. The role parameter tunes selection:
//
//   ANY        — first match; usually the primary
//   PRIMARY    — strictly the primary provider
//   SECONDARY  — strictly a non-primary provider
//   EXCLUSIVE  — primary, AND register a hold
pm_peer_t* pm_peer_find(const char* capability, pm_peer_role_t role);

// Release an exclusive hold previously obtained.
void pm_peer_release(pm_peer_t* p);

// ── Capability call ──────────────────────────────────────────
//
// Generic call into a peer. The transport (bridge UART, SPI,
// I2C, internal function call) is encapsulated by the peer.
//
// `op` is a small string ("scan", "tx", "read_tag", etc.) —
// each capability defines its own ops vocabulary.
// `params` is a JSON-ish string (the bridge transports use JSON;
// in-process peers can ignore it).
// Returns 0 on success, negative on error.
int pm_peer_call(pm_peer_t* p, const char* op,
                  const char* params);

// ── Enumeration ──────────────────────────────────────────────
//
// Walk all registered peers (for the SYSTEM/About screen).
int  pm_peer_count(void);
const pm_peer_t* pm_peer_at(int idx);

// ── Capability listing ───────────────────────────────────────
//
// All capabilities a peer offers (NULL-terminated array of
// pointers to capability strings).
const char* const* pm_peer_capabilities(const pm_peer_t* p);

// ── Hot-plug ─────────────────────────────────────────────────
//
// Bridge handlers call these when peers come and go.
void pm_peer_announce(pm_peer_kind_t kind, const char* name,
                       const char* const* caps);
void pm_peer_withdraw(pm_peer_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif  // PM_PEER_H
