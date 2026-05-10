// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// pm_app_probe_intel.h — Probe request intelligence
// (v1.1.1 had this built but unwired — Phase 8 lights it up.)
// Two modes: scan (active wifi probe-resp) and promiscuous
// (capture all probe-req frames). Shows MAC→SSID-list table
// — devices broadcasting their preferred network names.
#ifndef PM_APP_PROBE_INTEL_H
#define PM_APP_PROBE_INTEL_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_probe_intel(void);
void pm_app_probe_intel_on_probe(const char* mac, const char* ssid,
                                   int rssi, int count);
#ifdef __cplusplus
}
#endif
#endif
