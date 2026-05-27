// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


#ifndef PM_APP_PKT_SNIFFER_H
#define PM_APP_PKT_SNIFFER_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_pkt_sniffer(void);
void pm_app_pkt_sniffer_on_pkt(const char* frame_type, const char* src, int rssi);
#ifdef __cplusplus
}
#endif
#endif
