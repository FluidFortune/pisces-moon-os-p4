// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// pm_app_pkt_analysis.h — Offline packet analysis
// Browses /sd/captures/*.pcap files (saved by pkt_sniffer or
// transferred from host). Lightweight pcap reader, frame list,
// per-frame decode for 802.11 mgmt frames.
#ifndef PM_APP_PKT_ANALYSIS_H
#define PM_APP_PKT_ANALYSIS_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_pkt_analysis(void);
#ifdef __cplusplus
}
#endif
#endif
