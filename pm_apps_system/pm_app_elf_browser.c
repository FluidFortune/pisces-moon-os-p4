// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com



// ============================================================
//  pm_app_elf_browser.c — ELF module browser
//
//  Phase 2 stub: enumerates /sd/elf/*.elf, parses manifests
//  if present, but the actual ELF loader is a separate
//  component (pm_elf_loader, RISC-V port). Until that
//  component is online, the launch button reports
//  "loader not available".
//
//  Manifest schema (S3-compatible):
//    name    : module display name
//    abi     : ABI version int
//    psram_required_kb : minimum PSRAM headroom in KB
//    category: optional informational tag
// ============================================================

#include "pm_app_elf_browser.h"
#include "pm_hal.h"
#include "lvgl.h"
#include "pm_ui.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "PM_ELF_BROWSER";

#define MAX_MODULES 32

typedef struct {
    char name[64];
    char path[160];
    int  abi;
    int  psram_required_kb;
    bool has_manifest;
} elf_module_t;

static elf_module_t s_modules[MAX_MODULES];
static int          s_module_cnt = 0;
static int          s_cursor     = 0;

// ─────────────────────────────────────────────
//  Scan /sd/elf/
// ─────────────────────────────────────────────
static bool _ends_with(const char* s, const char* suffix) {
    size_t ls = strlen(s);
    size_t lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    return strcasecmp(s + ls - lsuf, suffix) == 0;
}

static void _try_parse_manifest(elf_module_t* m) {
    char json_path[200];
    snprintf(json_path, sizeof(json_path), "%s.json", m->path);

    pm_file_t* f = pm_file_open(json_path, PM_FILE_READ);
    if (!f) return;

    char buf[512];
    size_t got = pm_file_read(f, buf, sizeof(buf) - 1);
    pm_file_close(f);
    if (got == 0) return;
    buf[got] = 0;

    // Tiny ad-hoc JSON scrape — keys are well-known.
    // For robustness, switch to cJSON later.
    const char* p;
    if ((p = strstr(buf, "\"name\""))) {
        const char* q = strchr(p, ':'); if (q) {
            const char* lq = strchr(q, '"');
            if (lq) {
                const char* rq = strchr(lq + 1, '"');
                if (rq) {
                    size_t len = rq - lq - 1;
                    if (len >= sizeof(m->name)) len = sizeof(m->name) - 1;
                    memcpy(m->name, lq + 1, len);
                    m->name[len] = 0;
                }
            }
        }
    }
    if ((p = strstr(buf, "\"abi\""))) {
        sscanf(p, "\"abi\" : %d", &m->abi);
    }
    if ((p = strstr(buf, "\"psram_required_kb\""))) {
        sscanf(p, "\"psram_required_kb\" : %d", &m->psram_required_kb);
    }
    m->has_manifest = true;
}

static void _scan_modules(void) {
    s_module_cnt = 0;
    s_cursor     = 0;

    PM_SPI_TAKE("elf_scan") {
        pm_dir_t* d = pm_dir_open("/sd/elf");
        if (d) {
            const char* name;
            bool is_dir;
            while ((name = pm_dir_next(d, &is_dir)) != NULL &&
                   s_module_cnt < MAX_MODULES) {
                if (is_dir) continue;
                if (!_ends_with(name, ".elf")) continue;

                elf_module_t* m = &s_modules[s_module_cnt++];
                memset(m, 0, sizeof(*m));
                strncpy(m->name, name, sizeof(m->name) - 1);
                snprintf(m->path, sizeof(m->path), "/sd/elf/%s", name);
                m->abi = -1;
                m->psram_required_kb = 0;

                _try_parse_manifest(m);
            }
            pm_dir_close(d);
        }
    } PM_SPI_GIVE();

    pm_log_i(TAG, "found %d ELF modules", s_module_cnt);
}

// ─────────────────────────────────────────────
//  Launch (stub — pm_elf_loader is a separate component)
// ─────────────────────────────────────────────
static void _launch_selected(void) {
    if (s_cursor < 0 || s_cursor >= s_module_cnt) return;
    elf_module_t* m = &s_modules[s_cursor];
    pm_log_w(TAG, "[stub] would launch '%s' from %s "
                  "(abi=%d, psram_req=%dKB) — pm_elf_loader pending",
             m->name, m->path, m->abi, m->psram_required_kb);
    // TODO when pm_elf_loader exists:
    //   pm_elf_loader_run(m->path, &manifest);
}

// ─────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────
static void _render(void) {
    // TODO_LVGL: list of modules with manifest meta, footer
    // showing "free PSRAM: X KB".
}

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────
static lv_obj_t* s_default_screen = NULL;

static void _build_screen(void) {
    s_default_screen = pm_ui_default_screen("ELF APPS",
        "ELF APPS app — UI ready");
}

static void _init(void) { _build_screen(); }

static void _enter(void) {
    if (s_default_screen) lv_screen_load(s_default_screen);
    pm_log_i(TAG, "enter");
    _scan_modules();
    _render();
    // TODO_LVGL: lv_scr_load(...)
}

static void _exit_(void) { pm_log_i(TAG, "exit"); }

static const pm_app_t _APP = {
    .id           = "elf_browser",
    .display_name = "ELF APPS",
    .category     = PM_CAT_SYSTEM,
    .icon_id      = 0,
    .init         = _init,
    .enter        = _enter,
    .tick         = NULL,
    .exit         = _exit_,
    .deinit       = NULL,
};

const pm_app_t* pm_app_elf_browser(void) {
    (void)_launch_selected;
    return &_APP;
}
