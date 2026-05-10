// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// pm_app_net_scanner.h — Network host discovery on connected SSID
// Asks C6 to do an ARP sweep + ICMP probe across the current
// subnet. Hosts come back as "host_seen" events (TBD in C6
// firmware). Shows live host table.
#ifndef PM_APP_NET_SCANNER_H
#define PM_APP_NET_SCANNER_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_net_scanner(void);
void pm_app_net_scanner_on_host(const char* ip, const char* mac,
                                  const char* hostname, int latency_ms);
#ifdef __cplusplus
}
#endif
#endif
