// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com

#ifndef PM_CARDPUTER_I2C_H
#define PM_CARDPUTER_I2C_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_CARDPUTER_I2C_ADDR          0x32
#define PM_CARDPUTER_I2C_VERSION       1
#define PM_CARDPUTER_I2C_NAME_LEN      24
#define PM_CARDPUTER_I2C_LORA_MAX      180
#define PM_CARDPUTER_I2C_WIFI_FRAME_MAX 96
#define PM_CARDPUTER_I2C_BLE_NAME_MAX   32
#define PM_CARDPUTER_I2C_BLE_META_MAX   24

typedef enum {
    PM_CARDPUTER_REG_WHOAMI      = 0x00,
    PM_CARDPUTER_REG_STATUS      = 0x01,
    PM_CARDPUTER_REG_GPS         = 0x02,
    PM_CARDPUTER_REG_KEY_POP     = 0x03,
    PM_CARDPUTER_REG_LORA_RX_POP = 0x04,
    PM_CARDPUTER_REG_LORA_TX     = 0x05,
    PM_CARDPUTER_REG_LORA_STATUS = 0x06,
    PM_CARDPUTER_REG_WIFI_CTRL   = 0x10,
    PM_CARDPUTER_REG_WIFI_FRAME_POP = 0x11,
    PM_CARDPUTER_REG_WIFI_STATUS = 0x12,
    PM_CARDPUTER_REG_PING        = 0x7f,
} pm_cardputer_i2c_reg_t;

typedef enum {
    PM_CARDPUTER_CAP_KEYBOARD = 1u << 0,
    PM_CARDPUTER_CAP_GPS      = 1u << 1,
    PM_CARDPUTER_CAP_LORA     = 1u << 2,
    PM_CARDPUTER_CAP_WIFI     = 1u << 3,
    PM_CARDPUTER_CAP_BLE      = 1u << 4,
    PM_CARDPUTER_CAP_WIFI_PROMISC = 1u << 5,
    PM_CARDPUTER_CAP_WIFI_SCAN    = 1u << 6,
} pm_cardputer_i2c_caps_t;

typedef struct __attribute__((packed)) {
    uint8_t  magic0;
    uint8_t  magic1;
    uint8_t  version;
    uint8_t  device_kind;
    uint32_t caps;
    char     name[PM_CARDPUTER_I2C_NAME_LEN];
} pm_cardputer_i2c_whoami_t;

typedef struct __attribute__((packed)) {
    uint8_t  magic0;
    uint8_t  magic1;
    uint8_t  version;
    uint8_t  flags;
    uint32_t uptime_ms;
    uint32_t caps;
    uint16_t queued_keys;
    uint16_t queued_lora;
    int32_t  heap_free;
} pm_cardputer_i2c_status_t;

typedef struct __attribute__((packed)) {
    uint8_t  valid;
    uint8_t  sats;
    uint8_t  fix_quality;
    uint8_t  reserved;
    int32_t  lat_e7;
    int32_t  lon_e7;
    int32_t  alt_cm;
    uint32_t age_ms;
} pm_cardputer_i2c_gps_t;

typedef struct __attribute__((packed)) {
    uint8_t  available;
    uint8_t  kind;
    uint8_t  down;
    uint8_t  modifiers;
    uint32_t code;
    uint32_t timestamp_ms;
} pm_cardputer_i2c_key_t;

typedef struct __attribute__((packed)) {
    uint8_t  reg;
    uint8_t  len;
    uint8_t  data[PM_CARDPUTER_I2C_LORA_MAX];
} pm_cardputer_i2c_lora_tx_t;

typedef struct __attribute__((packed)) {
    uint8_t  available;
    uint8_t  len;
    int8_t   rssi;
    int8_t   snr_x4;
    uint32_t freq_khz;
    uint8_t  data[PM_CARDPUTER_I2C_LORA_MAX];
} pm_cardputer_i2c_lora_rx_t;

typedef enum {
    PM_CARDPUTER_WIFI_OP_PROMISC_STOP  = 0,
    PM_CARDPUTER_WIFI_OP_PROMISC_START = 1,
    PM_CARDPUTER_WIFI_OP_SET_CHANNEL   = 2,
} pm_cardputer_i2c_wifi_op_t;

typedef struct __attribute__((packed)) {
    uint8_t reg;
    uint8_t op;
    uint8_t channel;
    uint8_t filter;
} pm_cardputer_i2c_wifi_ctrl_t;

typedef struct __attribute__((packed)) {
    uint8_t  available;
    uint8_t  frame_type;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  mac[6];
    uint16_t len;
    uint8_t  data[PM_CARDPUTER_I2C_WIFI_FRAME_MAX];
} pm_cardputer_i2c_wifi_frame_t;

typedef struct {
    uint8_t available;
    char    mac[18];
    char    name[PM_CARDPUTER_I2C_BLE_NAME_MAX];
    int8_t  rssi;
    char    addr_type[PM_CARDPUTER_I2C_BLE_META_MAX];
    char    mfg[PM_CARDPUTER_I2C_BLE_META_MAX];
} pm_cardputer_i2c_ble_seen_t;

// Probe/arm once at boot. Current P4 board profiles use UART1 as the
// Cardputer event pipe; the original I2C register protocol remains available
// behind the same API for low-speed control experiments.
bool pm_cardputer_i2c_init_auto(void);

bool pm_cardputer_i2c_present(void);
bool pm_cardputer_i2c_link_seen(void);
uint32_t pm_cardputer_i2c_caps(void);

esp_err_t pm_cardputer_i2c_read_whoami(pm_cardputer_i2c_whoami_t* out);
esp_err_t pm_cardputer_i2c_read_status(pm_cardputer_i2c_status_t* out);
esp_err_t pm_cardputer_i2c_read_gps(pm_cardputer_i2c_gps_t* out);
esp_err_t pm_cardputer_i2c_poll_key(pm_cardputer_i2c_key_t* out);
esp_err_t pm_cardputer_i2c_lora_tx(const uint8_t* data, uint8_t len);
esp_err_t pm_cardputer_i2c_lora_rx_pop(pm_cardputer_i2c_lora_rx_t* out);
esp_err_t pm_cardputer_i2c_wifi_promisc_start(uint8_t channel, uint8_t filter);
esp_err_t pm_cardputer_i2c_wifi_set_channel(uint8_t channel);
esp_err_t pm_cardputer_i2c_wifi_promisc_stop(void);
esp_err_t pm_cardputer_i2c_wifi_frame_pop(pm_cardputer_i2c_wifi_frame_t* out);
esp_err_t pm_cardputer_i2c_ble_scan_start(bool active);
esp_err_t pm_cardputer_i2c_ble_scan_stop(void);
esp_err_t pm_cardputer_i2c_ble_seen_pop(pm_cardputer_i2c_ble_seen_t* out);

// Peer-registry dispatcher. `op` accepts: ping, status, gps_get, key_poll,
// lora_tx, lora_rx_pop, wifi_promisc_start/promiscuous_start,
// wifi_promisc_stop/promiscuous_stop, wifi_frame_pop, ble_scan_start,
// ble_scan_stop, and ble_seen_pop. Returns 0 on success and a negative value
// on transport/protocol failure.
int pm_cardputer_i2c_call(const char* op, const char* params);

#ifdef __cplusplus
}
#endif

#endif  // PM_CARDPUTER_I2C_H
