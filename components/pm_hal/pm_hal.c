// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_hal.c — Hardware Abstraction Layer implementation
//
//  Maps the pm_hal API to ESP-IDF 5.4.x primitives:
//    - esp_log         for logging
//    - esp_timer       for time
//    - heap_caps       for PSRAM / SRAM allocators
//    - FreeRTOS        for mutex, delay
//    - VFS / FATFS     for file I/O
//    - GPIO driver     for pin control
//    - esp_random      for RNG
// ============================================================

#include "pm_hal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"

// SD/FAT — included only in the .c so apps don't see esp_vfs_fat
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char* TAG = "PM_HAL";

// ── Globals ──────────────────────────────────────────────────
pm_mutex_t pm_spi_treaty = NULL;

static bool        s_sd_mounted = false;
static sdmmc_card_t* s_sd_card  = NULL;

#define SD_MOUNT_POINT  "/sd"

// ─────────────────────────────────────────────────────────────
//  Logging
// ─────────────────────────────────────────────────────────────
void pm_log_i(const char* tag, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ESP_LOGI(tag, "%s", buf);
}

void pm_log_w(const char* tag, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ESP_LOGW(tag, "%s", buf);
}

void pm_log_e(const char* tag, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ESP_LOGE(tag, "%s", buf);
}

void pm_log_d(const char* tag, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ESP_LOGD(tag, "%s", buf);
}

// ─────────────────────────────────────────────────────────────
//  Time
// ─────────────────────────────────────────────────────────────
uint32_t pm_millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

uint64_t pm_micros(void) {
    return (uint64_t)esp_timer_get_time();
}

void pm_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void pm_delay_us(uint32_t us) {
    // For sub-tick precision; uses busy wait.
    int64_t end = esp_timer_get_time() + (int64_t)us;
    while (esp_timer_get_time() < end) { /* spin */ }
}

bool pm_time_now(pm_time_t* out) {
    if (!out) return false;
    time_t now = time(NULL);
    if (now < 100000) {
        // Not synced
        memset(out, 0, sizeof(*out));
        out->synced = false;
        return false;
    }
    struct tm t;
    localtime_r(&now, &t);
    out->year   = t.tm_year + 1900;
    out->month  = t.tm_mon + 1;
    out->day    = t.tm_mday;
    out->hour   = t.tm_hour;
    out->minute = t.tm_min;
    out->second = t.tm_sec;
    out->synced = true;
    return true;
}

// ─────────────────────────────────────────────────────────────
//  PSRAM / SRAM allocators
// ─────────────────────────────────────────────────────────────
void* pm_psram_alloc(size_t bytes) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void* pm_psram_calloc(size_t n, size_t size) {
    return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void* pm_psram_realloc(void* ptr, size_t bytes) {
    return heap_caps_realloc(ptr, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void pm_psram_free(void* ptr) {
    if (ptr) heap_caps_free(ptr);
}

size_t pm_psram_free_bytes(void) {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

size_t pm_psram_largest_free_block(void) {
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

void* pm_sram_alloc(size_t bytes) {
    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void pm_sram_free(void* ptr) {
    if (ptr) heap_caps_free(ptr);
}

// ─────────────────────────────────────────────────────────────
//  Mutex
// ─────────────────────────────────────────────────────────────
pm_mutex_t pm_mutex_create(void) {
    return (pm_mutex_t)xSemaphoreCreateMutex();
}

void pm_mutex_destroy(pm_mutex_t m) {
    if (m) vSemaphoreDelete((SemaphoreHandle_t)m);
}

bool pm_mutex_take(pm_mutex_t m, uint32_t timeout_ms) {
    if (!m) return false;
    return xSemaphoreTake((SemaphoreHandle_t)m,
                          pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void pm_mutex_give(pm_mutex_t m) {
    if (m) xSemaphoreGive((SemaphoreHandle_t)m);
}

// ─────────────────────────────────────────────────────────────
//  File I/O — thin wrapper over standard fopen/fread/fwrite.
//  ESP-IDF VFS makes "/sd/foo.txt" work with fopen() once
//  esp_vfs_fat_sdmmc_mount() has been called.
// ─────────────────────────────────────────────────────────────
struct pm_file_s {
    FILE* fp;
};

struct pm_dir_s {
    DIR* dp;
    struct dirent* ent;
};

static const char* _mode_string(int flags) {
    bool r = (flags & PM_FILE_READ);
    bool w = (flags & PM_FILE_WRITE);
    bool a = (flags & PM_FILE_APPEND);
    bool t = (flags & PM_FILE_TRUNC);

    if (a)        return "ab+";
    if (w && r)   return t ? "wb+" : "rb+";
    if (w)        return "wb";
    if (r)        return "rb";
    return "rb";
}

pm_file_t* pm_file_open(const char* path, int flags) {
    if (!path) return NULL;
    FILE* fp = fopen(path, _mode_string(flags));
    if (!fp) return NULL;
    pm_file_t* f = (pm_file_t*)calloc(1, sizeof(pm_file_t));
    if (!f) { fclose(fp); return NULL; }
    f->fp = fp;
    return f;
}

void pm_file_close(pm_file_t* f) {
    if (!f) return;
    if (f->fp) fclose(f->fp);
    free(f);
}

size_t pm_file_read(pm_file_t* f, void* buf, size_t bytes) {
    if (!f || !f->fp || !buf) return 0;
    return fread(buf, 1, bytes, f->fp);
}

size_t pm_file_write(pm_file_t* f, const void* buf, size_t bytes) {
    if (!f || !f->fp || !buf) return 0;
    return fwrite(buf, 1, bytes, f->fp);
}

int pm_file_printf(pm_file_t* f, const char* fmt, ...) {
    if (!f || !f->fp) return -1;
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f->fp, fmt, ap);
    va_end(ap);
    return r;
}

bool pm_file_seek(pm_file_t* f, size_t pos) {
    if (!f || !f->fp) return false;
    return fseek(f->fp, (long)pos, SEEK_SET) == 0;
}

size_t pm_file_tell(pm_file_t* f) {
    if (!f || !f->fp) return 0;
    long p = ftell(f->fp);
    return p < 0 ? 0 : (size_t)p;
}

size_t pm_file_size(pm_file_t* f) {
    if (!f || !f->fp) return 0;
    long cur = ftell(f->fp);
    if (cur < 0) return 0;
    if (fseek(f->fp, 0, SEEK_END) != 0) return 0;
    long end = ftell(f->fp);
    fseek(f->fp, cur, SEEK_SET);
    return end < 0 ? 0 : (size_t)end;
}

bool pm_file_eof(pm_file_t* f) {
    if (!f || !f->fp) return true;
    return feof(f->fp) != 0;
}

void pm_file_flush(pm_file_t* f) {
    if (f && f->fp) fflush(f->fp);
}

bool pm_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool pm_file_remove(const char* path) {
    return unlink(path) == 0;
}

bool pm_file_rename(const char* from, const char* to) {
    return rename(from, to) == 0;
}

bool pm_file_mkdir(const char* path) {
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

pm_dir_t* pm_dir_open(const char* path) {
    DIR* dp = opendir(path);
    if (!dp) return NULL;
    pm_dir_t* d = (pm_dir_t*)calloc(1, sizeof(pm_dir_t));
    if (!d) { closedir(dp); return NULL; }
    d->dp = dp;
    return d;
}

const char* pm_dir_next(pm_dir_t* d, bool* is_dir) {
    if (!d || !d->dp) return NULL;
    d->ent = readdir(d->dp);
    if (!d->ent) return NULL;
    if (is_dir) *is_dir = (d->ent->d_type == DT_DIR);
    return d->ent->d_name;
}

void pm_dir_close(pm_dir_t* d) {
    if (!d) return;
    if (d->dp) closedir(d->dp);
    free(d);
}

bool pm_sd_mounted(void) {
    return s_sd_mounted;
}

bool pm_sd_mount(void) {
    if (s_sd_mounted) return true;

    // Configuration is stub — actual GPIO pins TBD from Eagle schematic.
    // The CrowPanel exposes onboard MicroSD; the slot is wired but the
    // exact host (SDMMC vs SPI) needs hardware confirmation.
    //
    // Default assumption: SDMMC 1-line mode for first bring-up.
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;     // 1-bit mode for compatibility

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT,
                                             &host, &slot_config,
                                             &mount_config, &s_sd_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(err));
        return false;
    }
    s_sd_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    return true;
}

void pm_sd_unmount(void) {
    if (!s_sd_mounted) return;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_sd_card);
    s_sd_card = NULL;
    s_sd_mounted = false;
}

// ─────────────────────────────────────────────────────────────
//  Boot / system
// ─────────────────────────────────────────────────────────────
void pm_hal_init(void) {
    ESP_LOGI(TAG, "Pisces Moon OS Pisces Moon P4 v%s — HAL init",
             PM_VERSION_STRING);

    if (!pm_spi_treaty) {
        pm_spi_treaty = pm_mutex_create();
        ESP_LOGI(TAG, "SPI Bus Treaty mutex created");
    }

    // SD mount is best-effort; absence is OK for early bring-up.
    pm_sd_mount();

    pm_chip_info_t info;
    pm_chip_info(&info);
    ESP_LOGI(TAG, "Chip: %s rev %d, %d cores, flash %u MB, psram %u MB",
             info.chip_name, info.revision, info.cores,
             (unsigned)(info.flash_bytes / (1024*1024)),
             (unsigned)(info.psram_bytes / (1024*1024)));
}

void pm_reboot(void) {
    ESP_LOGW(TAG, "Reboot requested");
    esp_restart();
}

uint32_t pm_uptime_seconds(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

size_t pm_free_heap(void) {
    return esp_get_free_heap_size();
}

void pm_chip_info(pm_chip_info_t* out) {
    if (!out) return;
    esp_chip_info_t info;
    esp_chip_info(&info);
    out->chip_name   = "ESP32-P4";   // CONFIG_IDF_TARGET-based string
    out->cores       = info.cores;
    out->revision    = info.revision;
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    out->flash_bytes = (size_t)flash_size;
    out->psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
}

// ─────────────────────────────────────────────────────────────
//  GPIO
// ─────────────────────────────────────────────────────────────
void pm_gpio_mode(int pin, pm_gpio_mode_t mode) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = (mode == PM_GPIO_OUTPUT) ? GPIO_MODE_OUTPUT
                                                  : GPIO_MODE_INPUT,
        .pull_up_en   = (mode == PM_GPIO_INPUT_PULLUP)
                        ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (mode == PM_GPIO_INPUT_PULLDOWN)
                        ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

int pm_gpio_read(int pin) {
    return gpio_get_level((gpio_num_t)pin);
}

void pm_gpio_write(int pin, int level) {
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

// ─────────────────────────────────────────────────────────────
//  Random
// ─────────────────────────────────────────────────────────────
uint32_t pm_random_u32(void) {
    return esp_random();
}

int pm_random_range(int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int)(esp_random() % span);
}

// ─────────────────────────────────────────────────────────────
//  Chip identity — base MAC lower 32 bits
// ─────────────────────────────────────────────────────────────
#include "esp_mac.h"

uint32_t pm_chip_mac_lower32(void) {
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        return 0;
    }
    return ((uint32_t)mac[2] << 24) |
           ((uint32_t)mac[3] << 16) |
           ((uint32_t)mac[4] <<  8) |
           ((uint32_t)mac[5]);
}

// ─────────────────────────────────────────────────────────────
//  CRC32 — zlib polynomial 0xEDB88320
//  Lazy table init; one-shot table fits in 1 KB. Computed on
//  first call and cached. Single-threaded fine; if there's
//  ever contention we can race the init harmlessly (same
//  result either way).
// ─────────────────────────────────────────────────────────────
static uint32_t s_crc_table[256];
static bool     s_crc_table_ready = false;

static void _crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(int32_t)(c & 1));
        s_crc_table[i] = c;
    }
    s_crc_table_ready = true;
}

uint32_t pm_crc32_update(uint32_t seed, const uint8_t* buf, size_t len) {
    if (!s_crc_table_ready) _crc_init();
    uint32_t c = seed ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = s_crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t pm_crc32(const uint8_t* buf, size_t len) {
    return pm_crc32_update(0, buf, len);
}

// ─────────────────────────────────────────────────────────────
//  SPI Treaty bus pin map — VERIFIED ELECROW Lesson 14
//
//  These pins are routed to the wireless module slot footprint
//  on the CrowPanel Advanced 7" (DHE04107D). The slot accepts
//  any of ELECROW's wireless module carriers (SX1262, nRF24,
//  ESP32-H2, ESP32-C6, Wi-Fi HaLow). Per the wiki: "The pin
//  assignments should not be modified, otherwise the wireless
//  module will not work."
// ─────────────────────────────────────────────────────────────
#define PM_SPI_DEFAULT_SCK    8
#define PM_SPI_DEFAULT_MISO   7
#define PM_SPI_DEFAULT_MOSI   6

int pm_hal_spi_sck_pin (void) { return PM_SPI_DEFAULT_SCK;  }
int pm_hal_spi_miso_pin(void) { return PM_SPI_DEFAULT_MISO; }
int pm_hal_spi_mosi_pin(void) { return PM_SPI_DEFAULT_MOSI; }
