// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_nosql.c — Document store
//
//  All file I/O wrapped in PM_SPI_TAKE("nosql"). Apps never
//  hold the mutex while iterating results.
// ============================================================

#include "pm_nosql.h"
#include "pm_hal.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "PM_NOSQL";

#define ROOT "/sd/data"

static void _category_dir(const char* category, char* out, size_t cap) {
    snprintf(out, cap, "%s/%s", ROOT, category);
}

void pm_nosql_path(const char* category, const char* id,
                    char* out_buf, size_t cap) {
    snprintf(out_buf, cap, "%s/%s/%s.json", ROOT, category, id);
}

bool pm_nosql_init(const char* category) {
    if (!category) return false;
    char dir[80];
    _category_dir(category, dir, sizeof(dir));

    bool ok = false;
    PM_SPI_TAKE("nosql_init") {
        pm_file_mkdir(ROOT);
        pm_file_mkdir(dir);
        ok = true;
    } PM_SPI_GIVE();
    return ok;
}

bool pm_nosql_exists(const char* category, const char* id) {
    if (!category || !id) return false;
    char path[160];
    pm_nosql_path(category, id, path, sizeof(path));
    bool ok = false;
    PM_SPI_TAKE("nosql_exists") {
        ok = pm_file_exists(path);
    } PM_SPI_GIVE();
    return ok;
}

int pm_nosql_list(const char* category,
                  char* ids, int max_ids, int id_size) {
    if (!category || !ids || max_ids <= 0 || id_size <= 4) return 0;

    char dir[80];
    _category_dir(category, dir, sizeof(dir));

    int count = 0;
    PM_SPI_TAKE("nosql_list") {
        pm_dir_t* d = pm_dir_open(dir);
        if (d) {
            const char* name;
            bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL && count < max_ids) {
                if (is_dir) continue;
                size_t nl = strlen(name);
                if (nl < 6) continue;
                if (strcmp(name + nl - 5, ".json") != 0) continue;
                int copy_n = (int)nl - 5;
                if (copy_n >= id_size) copy_n = id_size - 1;
                char* slot = ids + count * id_size;
                memcpy(slot, name, copy_n);
                slot[copy_n] = 0;
                count++;
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();

    pm_log_d(TAG, "list '%s': %d entries", category, count);
    return count;
}

size_t pm_nosql_read(const char* category, const char* id,
                     char* out_buf, size_t cap) {
    if (!category || !id || !out_buf || cap == 0) return 0;
    char path[160];
    pm_nosql_path(category, id, path, sizeof(path));

    size_t got = 0;
    PM_SPI_TAKE("nosql_read") {
        pm_file_t* f = pm_file_open(path, PM_FILE_READ);
        if (f) {
            got = pm_file_read(f, out_buf, cap - 1);
            out_buf[got] = 0;
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    return got;
}

bool pm_nosql_write(const char* category, const char* id,
                    const char* json, size_t len) {
    if (!category || !id || !json) return false;
    pm_nosql_init(category);     // ensure dir
    char path[160];
    pm_nosql_path(category, id, path, sizeof(path));

    bool ok = false;
    PM_SPI_TAKE("nosql_write") {
        pm_file_t* f = pm_file_open(path,
            PM_FILE_WRITE | PM_FILE_CREATE | PM_FILE_TRUNC);
        if (f) {
            ok = (pm_file_write(f, json, len) == len);
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    return ok;
}

bool pm_nosql_append(const char* category, const char* id,
                     const char* json, size_t len) {
    if (!category || !id || !json) return false;
    pm_nosql_init(category);
    char path[160];
    pm_nosql_path(category, id, path, sizeof(path));

    bool ok = false;
    PM_SPI_TAKE("nosql_append") {
        pm_file_t* f = pm_file_open(path,
            PM_FILE_APPEND | PM_FILE_CREATE);
        if (f) {
            ok = (pm_file_write(f, json, len) == len);
            pm_file_write(f, "\n", 1);
            pm_file_close(f);
        }
    } PM_SPI_GIVE();
    return ok;
}

bool pm_nosql_delete(const char* category, const char* id) {
    if (!category || !id) return false;
    char path[160];
    pm_nosql_path(category, id, path, sizeof(path));
    bool ok = false;
    PM_SPI_TAKE("nosql_delete") {
        ok = pm_file_remove(path);
    } PM_SPI_GIVE();
    return ok;
}
