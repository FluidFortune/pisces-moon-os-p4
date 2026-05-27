// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_ssh.h — SSH client (libssh-mbedtls)
//
//  S3 used libssh-esp32. P4/RISC-V port path:
//    1. mbedTLS is built-in ESP-IDF.
//    2. libssh works on top of mbedTLS — port is non-trivial
//       but doable. ESP-IDF managed components has wolfSSH /
//       libssh-mbedtls as third-party options worth surveying.
//    3. Networking is via the C6 — same TCP-over-bridge layer
//       needed by terminal/baseball/trails.
//
//  Phase-4 stub: connection profile UI + a non-functional
//  "Connect" button. Profiles are saved to NoSQL category
//  "ssh_profiles" so they persist across sessions.
// ============================================================

#ifndef PM_APP_SSH_H
#define PM_APP_SSH_H

#include "pm_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[32];      // user-visible label
    char host[64];
    int  port;          // default 22
    char user[32];
    char auth[16];      // "password" | "key"
    // Note: passwords/keys NOT persisted on device.
    //       Each connect prompts for credentials in-session.
} pm_ssh_profile_t;

const pm_app_t* pm_app_ssh(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_APP_SSH_H
