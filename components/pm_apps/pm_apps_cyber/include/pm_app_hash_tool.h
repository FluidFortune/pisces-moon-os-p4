// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// pm_app_hash_tool.h — Hash & integrity tool
// Computes SHA256/MD5/CRC32 over text input or files on SD.
// Used for chain-of-custody on session DBs and CSVs (see
// PiscesMoon vision doc for the audit hash workflow).
#ifndef PM_APP_HASH_TOOL_H
#define PM_APP_HASH_TOOL_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_hash_tool(void);
#ifdef __cplusplus
}
#endif
#endif
