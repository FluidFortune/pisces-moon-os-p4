// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_app_retro_elf.h — Retro ELF pack
//
//  Lists ELF modules in /sd/elf/games/ and launches them
//  through pm_elf_loader (RISC-V port pending).
//
//  This is the launcher / metadata viewer — the actual
//  emulators (NES, Game Boy, Atari, etc.) live as ELF
//  modules and are loaded on demand. Same architecture as
//  pm_app_elf_browser but filtered to the games subfolder.
// ============================================================

#ifndef PM_APP_RETRO_ELF_H
#define PM_APP_RETRO_ELF_H
#include "pm_app.h"
#ifdef __cplusplus
extern "C" {
#endif
const pm_app_t* pm_app_retro_elf(void);
#ifdef __cplusplus
}
#endif
#endif
