// Pisces Moon OS
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// Contributions: see CLA.md
// fluidfortune.com


// ============================================================
//  pm_hal.h — Hardware Abstraction Layer
//
//  Replaces the Arduino API surface used by the S3 codebase
//  with ESP-IDF native equivalents. Every app includes this.
//
//  Replaces:
//    Serial.print*    → pm_log_*
//    ps_malloc        → pm_psram_alloc
//    millis()         → pm_millis()
//    delay()          → pm_delay_ms()
//    SemaphoreHandle  → pm_mutex_t (passthrough but typed)
//    File / SdFat     → pm_file_t (fatfs wrapper)
//    Arduino_GFX*     → (removed — apps draw via LVGL directly)
//
//  Apps should NOT include esp_*.h directly except for things
//  pm_hal does not yet wrap. If you find yourself reaching past
//  pm_hal, add the wrapper here so the next app benefits.
// ============================================================

#ifndef PM_HAL_H
#define PM_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Build identity ───────────────────────────────────────────
#define PM_VERSION_MAJOR    2
#define PM_VERSION_MINOR    0
#define PM_VERSION_PATCH    0
#define PM_VERSION_STRING   "2.0.0-p4-dev"

// ── Logging ──────────────────────────────────────────────────
// Thin wrapper over ESP_LOG* — apps pass their own tag.
// All output goes to USB-CDC serial console on UART0.
void pm_log_i(const char* tag, const char* fmt, ...);
void pm_log_w(const char* tag, const char* fmt, ...);
void pm_log_e(const char* tag, const char* fmt, ...);
void pm_log_d(const char* tag, const char* fmt, ...);

// ── Time ─────────────────────────────────────────────────────
uint32_t pm_millis(void);              // ms since boot
uint64_t pm_micros(void);              // us since boot
void     pm_delay_ms(uint32_t ms);     // FreeRTOS vTaskDelay
void     pm_delay_us(uint32_t us);     // busy-wait, use sparingly

// Wall-clock (NTP-set if available, else 0)
typedef struct {
    int year, month, day;       // year is full (2026), month 1-12
    int hour, minute, second;
    bool synced;                // false if no NTP/RTC sync yet
} pm_time_t;

bool pm_time_now(pm_time_t* out);

// ── PSRAM allocator ──────────────────────────────────────────
// P4 has 32MB PSRAM. SQLite + LVGL framebuffers + ELF region
// all live there. ~20MB headroom for app use.
void* pm_psram_alloc(size_t bytes);
void* pm_psram_calloc(size_t n, size_t size);
void* pm_psram_realloc(void* ptr, size_t bytes);
void  pm_psram_free(void* ptr);
size_t pm_psram_free_bytes(void);
size_t pm_psram_largest_free_block(void);

// Internal SRAM (768KB on P4) — fast, scarce. Use for hot paths only.
void* pm_sram_alloc(size_t bytes);
void  pm_sram_free(void* ptr);

// ── Mutex ────────────────────────────────────────────────────
// Wraps FreeRTOS SemaphoreHandle_t. Used by SPI Bus Treaty.
typedef void* pm_mutex_t;

pm_mutex_t pm_mutex_create(void);
void       pm_mutex_destroy(pm_mutex_t m);
bool       pm_mutex_take(pm_mutex_t m, uint32_t timeout_ms);  // false on timeout
void       pm_mutex_give(pm_mutex_t m);

// ── File I/O ─────────────────────────────────────────────────
// Backed by ESP-IDF fatfs (SD card) and littlefs (internal flash
// "spiffs" partition — kept as fallback only, name retained for
// historical compatibility with S3 partition layout).
//
// Path conventions:
//   "/sd/..."     → MicroSD card (FAT32)
//   "/fs/..."     → Internal flash LittleFS partition
//   "/gamedata/..." → Internal flash FAT partition (Doom WAD etc.)
//
// All paths are absolute. No CWD concept.

typedef struct pm_file_s pm_file_t;

typedef enum {
    PM_FILE_READ   = 1,
    PM_FILE_WRITE  = 2,
    PM_FILE_APPEND = 4,
    PM_FILE_CREATE = 8,
    PM_FILE_TRUNC  = 16,
} pm_file_mode_t;

pm_file_t* pm_file_open(const char* path, int mode_flags);
void       pm_file_close(pm_file_t* f);
size_t     pm_file_read(pm_file_t* f, void* buf, size_t bytes);
size_t     pm_file_write(pm_file_t* f, const void* buf, size_t bytes);
int        pm_file_printf(pm_file_t* f, const char* fmt, ...);
bool       pm_file_seek(pm_file_t* f, size_t pos);
size_t     pm_file_tell(pm_file_t* f);
size_t     pm_file_size(pm_file_t* f);
bool       pm_file_eof(pm_file_t* f);
void       pm_file_flush(pm_file_t* f);

bool       pm_file_exists(const char* path);
bool       pm_file_remove(const char* path);
bool       pm_file_rename(const char* from, const char* to);
bool       pm_file_mkdir(const char* path);

// Directory iteration
typedef struct pm_dir_s pm_dir_t;

pm_dir_t*   pm_dir_open(const char* path);
const char* pm_dir_next(pm_dir_t* d, bool* is_dir);  // returns NULL at end
void        pm_dir_close(pm_dir_t* d);

// Mount status
bool pm_sd_mounted(void);
bool pm_sd_mount(void);     // Called once at boot
void pm_sd_unmount(void);

// ── SPI Bus Treaty ───────────────────────────────────────────
// On P4, scope narrows to LoRa-vs-SD on the P4 side only.
// Radio/WiFi/BLE are on the C6 — they don't touch this mutex.
extern pm_mutex_t pm_spi_treaty;

#define PM_SPI_TIMEOUT_MS  500

// Convenience macros for treaty discipline.
//   PM_SPI_TAKE("wardrive_log") { ...sd writes here... } PM_SPI_GIVE();
#define PM_SPI_TAKE(who) \
    if (pm_mutex_take(pm_spi_treaty, PM_SPI_TIMEOUT_MS)) { \
        (void)(who);

#define PM_SPI_GIVE() \
        pm_mutex_give(pm_spi_treaty); \
    }

// ── Boot / system ────────────────────────────────────────────
void     pm_hal_init(void);            // Called once from main()
void     pm_reboot(void);
uint32_t pm_uptime_seconds(void);
size_t   pm_free_heap(void);

// Chip info
typedef struct {
    const char* chip_name;     // "ESP32-P4"
    int         cores;
    int         revision;
    size_t      flash_bytes;
    size_t      psram_bytes;
} pm_chip_info_t;

void pm_chip_info(pm_chip_info_t* out);

// ── GPIO (thin wrapper, pin numbers TBD from Eagle schematic) ──
typedef enum {
    PM_GPIO_INPUT,
    PM_GPIO_INPUT_PULLUP,
    PM_GPIO_INPUT_PULLDOWN,
    PM_GPIO_OUTPUT,
} pm_gpio_mode_t;

void pm_gpio_mode(int pin, pm_gpio_mode_t mode);
int  pm_gpio_read(int pin);
void pm_gpio_write(int pin, int level);

// ── Random ───────────────────────────────────────────────────
uint32_t pm_random_u32(void);
int      pm_random_range(int min, int max);  // inclusive

// ── Chip identity ────────────────────────────────────────────
// Lower 32 bits of the device base MAC. Stable across boots.
// Used as NodeID in mesh_messenger and as a ducky session key.
uint32_t pm_chip_mac_lower32(void);

// ── CRC32 ────────────────────────────────────────────────────
// One-shot and streaming. Polynomial 0xEDB88320 (zlib/PNG).
// Initial seed is 0; pass returned value back as `seed` to
// continue across multiple buffers.
uint32_t pm_crc32(const uint8_t* buf, size_t len);
uint32_t pm_crc32_update(uint32_t seed, const uint8_t* buf, size_t len);

// ── SPI Treaty bus pins ──────────────────────────────────────
// Returned for components that want raw SPI access (e.g. the
// pm_lora wrapper around RadioLib's EspHal). The Treaty mutex
// is still required for any actual transaction — these getters
// only expose pin numbers.
int pm_hal_spi_sck_pin (void);
int pm_hal_spi_miso_pin(void);
int pm_hal_spi_mosi_pin(void);

#ifdef __cplusplus
}
#endif

#endif  // PM_HAL_H
