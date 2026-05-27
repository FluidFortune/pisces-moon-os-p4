// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_c6_bridge.h — P4 side receiver for C6 Ghost Engine stream
//
//  The P4 treats the C6 exactly like a connected T-Deck in
//  Bridge mode. Same JSON parsing. Same event dispatch.
//  Same protocol. Different physical connection (UART vs USB).
//
//  UART: UART2 on P4 side (maps to C6's UART1)
//  Baud: 921600
//
//  Adapted from the project's c6_bridge.h with C-only renaming
//  and pm_ namespace.
// ============================================================

#ifndef PM_C6_BRIDGE_H
#define PM_C6_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_C6_UART_NUM   2          // P4 UART2 → C6 UART1
#define PM_C6_BAUD       921600
#define PM_C6_TX_PIN     -1         // TBD — verify from Eagle schematic
#define PM_C6_RX_PIN     -1         // TBD — verify from Eagle schematic

// ── Event callbacks ──────────────────────────────────────────
// All strings are heap-borrowed: valid only during the callback.
// If the callback needs to retain a string, it must copy it.

typedef void (*pm_c6_wifi_cb)(const char* mac, const char* ssid,
                              int rssi, int ch, const char* enc,
                              double lat, double lng);

typedef void (*pm_c6_ble_cb)(const char* mac, const char* name,
                             int rssi, double lat, double lng,
                             const char* addr_type,
                             const char* mfg_data);

typedef void (*pm_c6_probe_cb)(const char* mac, const char* ssid,
                               int rssi, int count,
                               double lat, double lng);

typedef void (*pm_c6_gps_cb)(double lat, double lng, double alt,
                             int sats, bool valid);

typedef void (*pm_c6_pkt_cb)(const char* frame_type,
                             const char* src, int rssi);

typedef void (*pm_c6_ready_cb)(const char* firmware,
                               const char* version);

typedef void (*pm_c6_status_cb)(bool wifi_active, bool ble_active,
                                int  networks_found, int ble_devices,
                                uint32_t uptime_s);

// ── Init ─────────────────────────────────────────────────────
typedef struct {
    pm_c6_wifi_cb    on_wifi;
    pm_c6_ble_cb     on_ble;
    pm_c6_probe_cb   on_probe;
    pm_c6_gps_cb     on_gps;
    pm_c6_pkt_cb     on_pkt;
    pm_c6_ready_cb   on_ready;
    pm_c6_status_cb  on_status;
} pm_c6_callbacks_t;

void pm_c6_bridge_init(const pm_c6_callbacks_t* cbs);
void pm_c6_bridge_task(void* pvArgs);   // pass to xTaskCreate

// ── Commands to C6 ───────────────────────────────────────────
void pm_c6_cmd_wardrive_start(void);
void pm_c6_cmd_wardrive_stop(void);
void pm_c6_cmd_ble_start(void);
void pm_c6_cmd_ble_stop(void);
void pm_c6_cmd_promiscuous_start(void);
void pm_c6_cmd_promiscuous_stop(void);
void pm_c6_cmd_raw_log_start(void);
void pm_c6_cmd_raw_log_stop(void);
void pm_c6_cmd_ping(void);
void pm_c6_cmd_status(void);

// Generic — for commands not yet wrapped above.
void pm_c6_cmd_send_raw(const char* json_line);

// ── State flags (set when C6 ready event arrives) ────────────
extern volatile bool pm_c6_connected;
extern volatile bool pm_c6_wardrive_active;
extern volatile bool pm_c6_ble_active;

#ifdef __cplusplus
}
#endif

#endif  // PM_C6_BRIDGE_H
