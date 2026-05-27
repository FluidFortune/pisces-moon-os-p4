// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_sqlite.h — SQLite wrapper, Treaty-aware
//
//  Wraps the siara-cc/sqlite3 managed component. Apps never
//  see raw sqlite3* handles. SPI Treaty discipline lives
//  inside open/close so consumers don't need to reason about
//  SD card contention with the Ghost Engine wardrive task.
//
//  Design notes:
//    - One DB per session for wardrive (the schema below).
//    - Apps that just want a quick query/exec use pm_db_exec.
//    - For row-by-row reads, pm_db_query iterates a cursor.
//    - CSV export is a generic helper — works on any table.
//
//  Fallback: if SQLite itself misbehaves (corrupt file, OOM,
//  etc.) wardrive can fall back to the per-session CSV format
//  carried over from the S3. The schema is laid out so each
//  table maps 1:1 to a CSV row shape.
// ============================================================

#ifndef PM_SQLITE_H
#define PM_SQLITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_db_s     pm_db_t;
typedef struct pm_stmt_s   pm_stmt_t;

// ── Init / open / close ─────────────────────────────────────
bool      pm_sqlite_global_init(void);   // call once at boot
pm_db_t*  pm_db_open(const char* path);
void      pm_db_close(pm_db_t* db);
const char* pm_db_last_error(pm_db_t* db);

// Apply a multi-statement schema in one shot.
bool      pm_db_apply_schema(pm_db_t* db, const char* schema_sql);

// ── Convenience — exec one statement, no rows expected ──────
bool      pm_db_exec(pm_db_t* db, const char* sql);

// ── Prepared statements / parameter bind ────────────────────
pm_stmt_t* pm_db_prepare(pm_db_t* db, const char* sql);
void       pm_stmt_finalize(pm_stmt_t* st);

bool       pm_stmt_bind_text(pm_stmt_t* st, int idx, const char* v);
bool       pm_stmt_bind_int (pm_stmt_t* st, int idx, int v);
bool       pm_stmt_bind_int64(pm_stmt_t* st, int idx, int64_t v);
bool       pm_stmt_bind_double(pm_stmt_t* st, int idx, double v);
bool       pm_stmt_bind_null(pm_stmt_t* st, int idx);

bool       pm_stmt_step(pm_stmt_t* st);     // true if a row was returned
bool       pm_stmt_reset(pm_stmt_t* st);

// Read columns from current row.
const char* pm_stmt_col_text(pm_stmt_t* st, int idx);
int         pm_stmt_col_int(pm_stmt_t* st, int idx);
int64_t     pm_stmt_col_int64(pm_stmt_t* st, int idx);
double      pm_stmt_col_double(pm_stmt_t* st, int idx);
int         pm_stmt_col_count(pm_stmt_t* st);

// ── CSV export ──────────────────────────────────────────────
// Streams each row of `select_sql` into out_path, with a
// header line of column names. Returns rows written, -1 on
// error. SPI Treaty handled internally.
int  pm_db_export_csv(pm_db_t* db,
                       const char* select_sql,
                       const char* out_path);

#ifdef __cplusplus
}
#endif

#endif  // PM_SQLITE_H
