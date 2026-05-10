// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_mesh_messenger.h — Meshtastic-compatible LoRa
//
//  IRC-style multi-channel messenger interoperating with
//  real Meshtastic nodes on the LongFast preset (US 906.875
//  MHz / EU 869.525 MHz, BW250kHz, SF11, CR4/8, sync 0x2B).
//
//  Same SX1262 hardware as lora_voice — they cannot run
//  simultaneously. Mutex guards the radio.
//
//  16-byte MeshHeader (dest/from/id/flags) + protobuf
//  TEXT_MESSAGE_APP payload. Channels 0-7.
//
//  NodeID = ESP32 MAC lower 4 bytes, computed once at init.
// ============================================================

#ifndef PM_APP_MESH_MESSENGER_H
#define PM_APP_MESH_MESSENGER_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_mesh_messenger(void);
void pm_app_mesh_messenger_send(const char* text);
void pm_app_mesh_messenger_set_channel(int ch);
#ifdef __cplusplus
}
#endif
#endif
