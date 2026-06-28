// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_wardrive_schema.h — Wardrive session DB schema
//
//  ONE database file per boot session at:
//    /sd/sessions/session_YYYYMMDD_HHMMSS.db
//
//  Tables map 1:1 to the events the C6 Ghost Engine emits.
//  CSV export of any single table is Jennifer-compatible.
//
//  All tables include lat/lng (snapped from pm_gps_state at
//  insert time) and a millisecond timestamp.
//
//  Indices are tuned for "show me everything I saw at place X
//  between time A and B" queries on Jennifer.
// ============================================================

#ifndef PM_WARDRIVE_SCHEMA_H
#define PM_WARDRIVE_SCHEMA_H

#define PM_WARDRIVE_SCHEMA_SQL                                   \
    "CREATE TABLE IF NOT EXISTS metadata ("                      \
    "  key TEXT PRIMARY KEY, value TEXT);"                       \
                                                                  \
    "CREATE TABLE IF NOT EXISTS wifi_seen ("                     \
    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"             \
    "  bssid     TEXT NOT NULL,"                                  \
    "  ssid      TEXT,"                                           \
    "  rssi      INTEGER,"                                        \
    "  channel   INTEGER,"                                        \
    "  enc       TEXT,"                                           \
    "  lat       REAL,"                                           \
    "  lng       REAL,"                                           \
    "  first_ms  INTEGER,"                                        \
    "  last_ms   INTEGER,"                                        \
    "  hits      INTEGER DEFAULT 1);"                             \
    "CREATE INDEX IF NOT EXISTS idx_wifi_bssid ON wifi_seen(bssid);" \
    "CREATE INDEX IF NOT EXISTS idx_wifi_last  ON wifi_seen(last_ms);" \
                                                                  \
    "CREATE TABLE IF NOT EXISTS ble_seen ("                      \
    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"             \
    "  mac       TEXT NOT NULL,"                                  \
    "  name      TEXT,"                                           \
    "  rssi      INTEGER,"                                        \
    "  addr_type TEXT,"                                           \
    "  mfg       TEXT,"                                           \
    "  lat       REAL,"                                           \
    "  lng       REAL,"                                           \
    "  first_ms  INTEGER,"                                        \
    "  last_ms   INTEGER,"                                        \
    "  hits      INTEGER DEFAULT 1);"                             \
    "CREATE INDEX IF NOT EXISTS idx_ble_mac  ON ble_seen(mac);"  \
    "CREATE INDEX IF NOT EXISTS idx_ble_last ON ble_seen(last_ms);"  \
                                                                  \
    "CREATE TABLE IF NOT EXISTS probes ("                        \
    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"             \
    "  mac       TEXT NOT NULL,"                                  \
    "  ssid      TEXT,"                                           \
    "  rssi      INTEGER,"                                        \
    "  lat       REAL,"                                           \
    "  lng       REAL,"                                           \
    "  first_ms  INTEGER,"                                        \
    "  last_ms   INTEGER,"                                        \
    "  hits      INTEGER DEFAULT 1);"                             \
    "CREATE INDEX IF NOT EXISTS idx_probe_mac  ON probes(mac);"  \
                                                                  \
    "CREATE TABLE IF NOT EXISTS packets ("                       \
    "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"            \
    "  frame_type TEXT,"                                          \
    "  src        TEXT,"                                          \
    "  dst        TEXT,"                                          \
    "  rssi       INTEGER,"                                       \
    "  lat        REAL,"                                          \
    "  lng        REAL,"                                          \
    "  ts_ms      INTEGER);"                                      \
    "CREATE INDEX IF NOT EXISTS idx_pkt_ts  ON packets(ts_ms);"  \
                                                                  \
    "CREATE TABLE IF NOT EXISTS gps_track ("                     \
    "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"             \
    "  lat       REAL,"                                           \
    "  lng       REAL,"                                           \
    "  alt_m     REAL,"                                           \
    "  sats      INTEGER,"                                        \
    "  ts_ms     INTEGER);"                                       \
                                                                  \
    "CREATE TABLE IF NOT EXISTS lora_seen ("                     \
    "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"          \
    "  node_id      TEXT NOT NULL,"                               \
    "  from_id      TEXT,"                                        \
    "  to_id        TEXT,"                                        \
    "  pkt_id       INTEGER,"                                     \
    "  port_num     INTEGER,"                                     \
    "  hop_limit    INTEGER,"                                     \
    "  want_ack     INTEGER,"                                     \
    "  rssi         INTEGER,"                                     \
    "  snr_x10      INTEGER,"                                     \
    "  freq_khz     INTEGER,"                                     \
    "  preset       TEXT,"                                        \
    "  payload_len  INTEGER,"                                     \
    "  payload_hash TEXT,"                                        \
    "  text_preview TEXT,"                                        \
    "  lat          REAL,"                                        \
    "  lng          REAL,"                                        \
    "  first_ms     INTEGER,"                                     \
    "  last_ms      INTEGER,"                                     \
    "  hits         INTEGER DEFAULT 1);"                          \
    "CREATE INDEX IF NOT EXISTS idx_lora_node ON lora_seen(node_id);" \
    "CREATE INDEX IF NOT EXISTS idx_lora_last ON lora_seen(last_ms);"

#endif  // PM_WARDRIVE_SCHEMA_H
