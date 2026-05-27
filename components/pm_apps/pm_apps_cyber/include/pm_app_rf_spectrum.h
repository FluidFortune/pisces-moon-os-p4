// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// pm_app_rf_spectrum.h — 2.4GHz channel utilization view
// Asks C6 to sweep each WiFi channel and report RSSI floor
// + utilization %. Renders 14-channel bar chart.
#ifndef PM_APP_RF_SPECTRUM_H
#define PM_APP_RF_SPECTRUM_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_rf_spectrum(void);
void pm_app_rf_spectrum_on_channel(int channel, int rssi_floor, int util_pct);
#ifdef __cplusplus
}
#endif
#endif
