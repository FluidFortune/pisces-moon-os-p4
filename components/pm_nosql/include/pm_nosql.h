// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_nosql.h — JSON document store on SD card
//
//  Carries forward the S3 nosql_store API. Documents live
//  under /sd/data/<category>/. Each document is one JSON file:
//
//    /sd/data/medical/index.json       ← list of doc IDs
//    /sd/data/medical/entry_001.json   ← one document
//
//  Apps using this layout in INTEL phase:
//    baseball   — players  (/sd/data/baseball)
//    trails     — trails   (/sd/data/trails)
//    ref_med    — medical  (/sd/data/medical)
//    ref_surv   — survival (/sd/data/survival)
//    gemini_log — chats    (/sd/data/gemini_log)
//
//  All operations take the SPI Treaty mutex internally —
//  callers do NOT need to wrap pm_nosql_* calls in
//  PM_SPI_TAKE/GIVE.
//
//  Future P4 upgrade: with 32MB PSRAM and SQLite available,
//  apps can opt into pm_sqlite instead. NoSQL kept as the
//  simple offline-first store.
// ============================================================

#ifndef PM_NOSQL_H
#define PM_NOSQL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize a category — creates /sd/data/<category>/ if missing.
bool pm_nosql_init(const char* category);

// List entry IDs in a category. Caller provides buffer of fixed-size
// strings. Returns the number of IDs filled. ids must be at least
// (max_ids * id_size) bytes.
int  pm_nosql_list(const char* category,
                   char* ids, int max_ids, int id_size);

// Read full document text into out_buf. Returns bytes read.
// out_buf is null-terminated on success.
size_t pm_nosql_read(const char* category, const char* id,
                      char* out_buf, size_t cap);

// Write document. Truncates and replaces.
bool pm_nosql_write(const char* category, const char* id,
                     const char* json, size_t len);

// Append-only write — adds a delimiter \n between writes. Useful for
// rolling logs. No replacement: use pm_nosql_write for that.
bool pm_nosql_append(const char* category, const char* id,
                      const char* json, size_t len);

// Delete document.
bool pm_nosql_delete(const char* category, const char* id);

// Existence check.
bool pm_nosql_exists(const char* category, const char* id);

// Path resolver — fills /sd/data/<category>/<id>.json into buffer.
void pm_nosql_path(const char* category, const char* id,
                    char* out_buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif  // PM_NOSQL_H
