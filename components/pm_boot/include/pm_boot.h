#pragma once
#include "lvgl.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_BOOT_OK       = 0,
    PM_BOOT_ACTIVE   = 1,
    PM_BOOT_WARN     = 2,
    PM_BOOT_FAIL     = 3,
    PM_BOOT_DISABLED = 4,
} pm_boot_status_t;

void pm_boot_screen_show(void);
void pm_boot_step(const char* label, const char* detail, pm_boot_status_t status);
void pm_boot_progress(int percent);
void pm_boot_splash_show(uint32_t duration_ms);
void pm_boot_dismiss(void);

#ifdef __cplusplus
}
#endif
