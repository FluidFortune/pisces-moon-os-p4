// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_sqlite.c — Wrapper over siara-cc/sqlite3 managed component
//
//  All public functions take/release the SPI Treaty mutex
//  around any operation that touches the SD card. SQLite
//  itself is single-threaded; we serialize at the wrapper
//  level so the Ghost Engine wardrive task never collides
//  with an app's read/write.
//
//  Fallback policy: if a function returns failure, callers
//  are expected to either retry or fall back to the CSV
//  path. Errors are logged at WARN level.
// ============================================================

#include "pm_sqlite.h"
#include "pm_hal.h"
#include "sqlite3.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "PM_SQL";

struct pm_db_s {
    sqlite3* h;
    char     last_error[160];
};

struct pm_stmt_s {
    sqlite3_stmt* h;
    pm_db_t*       db;
};

// ─────────────────────────────────────────────
//  Global init — usually a no-op once
// ─────────────────────────────────────────────
bool pm_sqlite_global_init(void) {
    int rc = sqlite3_initialize();
    if (rc != SQLITE_OK) {
        pm_log_e(TAG, "sqlite3_initialize failed: %d", rc);
        return false;
    }
    pm_log_i(TAG, "sqlite3 init OK");
    return true;
}

// ─────────────────────────────────────────────
//  Open / close
// ─────────────────────────────────────────────
pm_db_t* pm_db_open(const char* path) {
    if (!path) return NULL;
    pm_db_t* db = (pm_db_t*)pm_psram_calloc(1, sizeof(*db));
    if (!db) return NULL;

    int rc = SQLITE_OK;
    PM_SPI_TAKE("db_open") {
        rc = sqlite3_open(path, &db->h);
    } PM_SPI_GIVE();

    if (rc != SQLITE_OK) {
        pm_log_w(TAG, "open '%s' failed: %d (%s)",
                 path, rc, db->h ? sqlite3_errmsg(db->h) : "(null)");
        if (db->h) sqlite3_close(db->h);
        pm_psram_free(db);
        return NULL;
    }
    pm_log_i(TAG, "opened '%s'", path);
    return db;
}

void pm_db_close(pm_db_t* db) {
    if (!db) return;
    if (db->h) {
        PM_SPI_TAKE("db_close") {
            sqlite3_close(db->h);
        } PM_SPI_GIVE();
    }
    pm_psram_free(db);
}

const char* pm_db_last_error(pm_db_t* db) {
    return (db && db->last_error[0]) ? db->last_error : "";
}

static void _set_error(pm_db_t* db, const char* msg) {
    if (!db || !msg) return;
    strncpy(db->last_error, msg, sizeof(db->last_error) - 1);
    db->last_error[sizeof(db->last_error) - 1] = 0;
}

// ─────────────────────────────────────────────
//  Schema apply (multi-statement)
// ─────────────────────────────────────────────
bool pm_db_apply_schema(pm_db_t* db, const char* schema_sql) {
    if (!db || !db->h || !schema_sql) return false;
    char* errmsg = NULL;
    int rc = SQLITE_OK;
    PM_SPI_TAKE("db_schema") {
        rc = sqlite3_exec(db->h, schema_sql, NULL, NULL, &errmsg);
    } PM_SPI_GIVE();
    if (rc != SQLITE_OK) {
        _set_error(db, errmsg ? errmsg : "schema error");
        pm_log_w(TAG, "schema: %s", errmsg ? errmsg : "(null)");
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────
//  exec
// ─────────────────────────────────────────────
bool pm_db_exec(pm_db_t* db, const char* sql) {
    if (!db || !db->h || !sql) return false;
    char* errmsg = NULL;
    int rc = SQLITE_OK;
    PM_SPI_TAKE("db_exec") {
        rc = sqlite3_exec(db->h, sql, NULL, NULL, &errmsg);
    } PM_SPI_GIVE();
    if (rc != SQLITE_OK) {
        _set_error(db, errmsg ? errmsg : "exec error");
        pm_log_d(TAG, "exec: %s [%s]", sql, errmsg ? errmsg : "?");
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────
//  Prepared statements
// ─────────────────────────────────────────────
pm_stmt_t* pm_db_prepare(pm_db_t* db, const char* sql) {
    if (!db || !db->h || !sql) return NULL;
    pm_stmt_t* st = (pm_stmt_t*)pm_psram_calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->db = db;
    int rc = SQLITE_OK;
    PM_SPI_TAKE("db_prepare") {
        rc = sqlite3_prepare_v2(db->h, sql, -1, &st->h, NULL);
    } PM_SPI_GIVE();
    if (rc != SQLITE_OK) {
        _set_error(db, sqlite3_errmsg(db->h));
        pm_log_w(TAG, "prepare: %s", sqlite3_errmsg(db->h));
        pm_psram_free(st);
        return NULL;
    }
    return st;
}

void pm_stmt_finalize(pm_stmt_t* st) {
    if (!st) return;
    if (st->h) {
        PM_SPI_TAKE("db_finalize") {
            sqlite3_finalize(st->h);
        } PM_SPI_GIVE();
    }
    pm_psram_free(st);
}

bool pm_stmt_bind_text(pm_stmt_t* st, int idx, const char* v) {
    if (!st || !st->h) return false;
    return sqlite3_bind_text(st->h, idx, v ? v : "", -1, SQLITE_TRANSIENT) == SQLITE_OK;
}
bool pm_stmt_bind_int(pm_stmt_t* st, int idx, int v) {
    if (!st || !st->h) return false;
    return sqlite3_bind_int(st->h, idx, v) == SQLITE_OK;
}
bool pm_stmt_bind_int64(pm_stmt_t* st, int idx, int64_t v) {
    if (!st || !st->h) return false;
    return sqlite3_bind_int64(st->h, idx, v) == SQLITE_OK;
}
bool pm_stmt_bind_double(pm_stmt_t* st, int idx, double v) {
    if (!st || !st->h) return false;
    return sqlite3_bind_double(st->h, idx, v) == SQLITE_OK;
}
bool pm_stmt_bind_null(pm_stmt_t* st, int idx) {
    if (!st || !st->h) return false;
    return sqlite3_bind_null(st->h, idx) == SQLITE_OK;
}

bool pm_stmt_step(pm_stmt_t* st) {
    if (!st || !st->h) return false;
    int rc = SQLITE_OK;
    PM_SPI_TAKE("db_step") {
        rc = sqlite3_step(st->h);
    } PM_SPI_GIVE();
    return rc == SQLITE_ROW;
}

bool pm_stmt_reset(pm_stmt_t* st) {
    if (!st || !st->h) return false;
    return sqlite3_reset(st->h) == SQLITE_OK;
}

const char* pm_stmt_col_text(pm_stmt_t* st, int idx) {
    if (!st || !st->h) return "";
    const unsigned char* t = sqlite3_column_text(st->h, idx);
    return t ? (const char*)t : "";
}
int     pm_stmt_col_int   (pm_stmt_t* st, int idx) { return st && st->h ? sqlite3_column_int(st->h, idx) : 0; }
int64_t pm_stmt_col_int64 (pm_stmt_t* st, int idx) { return st && st->h ? sqlite3_column_int64(st->h, idx) : 0; }
double  pm_stmt_col_double(pm_stmt_t* st, int idx) { return st && st->h ? sqlite3_column_double(st->h, idx) : 0.0; }
int     pm_stmt_col_count (pm_stmt_t* st)          { return st && st->h ? sqlite3_column_count(st->h) : 0; }

// ─────────────────────────────────────────────
//  CSV export
//  Streams "col1,col2,...\n" header then each row.
//  Quotes any field containing comma, quote, newline.
// ─────────────────────────────────────────────
static void _csv_write_field(pm_file_t* f, const char* s) {
    if (!s) s = "";
    bool needs_quote = false;
    for (const char* p = s; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { needs_quote = true; break; }
    }
    if (!needs_quote) {
        pm_file_write(f, s, strlen(s));
        return;
    }
    pm_file_write(f, "\"", 1);
    for (const char* p = s; *p; p++) {
        if (*p == '"') pm_file_write(f, "\"\"", 2);
        else           pm_file_write(f, p, 1);
    }
    pm_file_write(f, "\"", 1);
}

int pm_db_export_csv(pm_db_t* db, const char* select_sql, const char* out_path) {
    if (!db || !db->h || !select_sql || !out_path) return -1;

    pm_stmt_t* st = pm_db_prepare(db, select_sql);
    if (!st) return -1;

    pm_file_t* f = NULL;
    PM_SPI_TAKE("csv_open") {
        f = pm_file_open(out_path, PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
    } PM_SPI_GIVE();
    if (!f) { pm_stmt_finalize(st); return -1; }

    int ncols = sqlite3_column_count(st->h);

    // Header — emit OUTSIDE the SPI mutex on purpose (small writes,
    // pm_file_write internally takes treaty for each call). For
    // throughput we batch into a small buffer and flush periodically.
    PM_SPI_TAKE("csv_hdr") {
        for (int i = 0; i < ncols; i++) {
            if (i) pm_file_write(f, ",", 1);
            const char* name = sqlite3_column_name(st->h, i);
            _csv_write_field(f, name ? name : "");
        }
        pm_file_write(f, "\n", 1);
    } PM_SPI_GIVE();

    int rows = 0;
    while (pm_stmt_step(st)) {
        PM_SPI_TAKE("csv_row") {
            for (int i = 0; i < ncols; i++) {
                if (i) pm_file_write(f, ",", 1);
                _csv_write_field(f, pm_stmt_col_text(st, i));
            }
            pm_file_write(f, "\n", 1);
        } PM_SPI_GIVE();
        rows++;
    }

    PM_SPI_TAKE("csv_close") {
        pm_file_flush(f);
        pm_file_close(f);
    } PM_SPI_GIVE();

    pm_stmt_finalize(st);
    pm_log_i(TAG, "exported %d rows → %s", rows, out_path);
    return rows;
}
