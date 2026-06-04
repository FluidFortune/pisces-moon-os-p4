/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_hosted.h"

#include "port_esp_hosted_host_log.h"

#include "esp_private/startup_internal.h"

DEFINE_LOG_TAG(host_init);

// Pisces Moon P4 initializes ESP-Hosted from app_main after BSP/LVGL
// are alive, so early transport failures can be shown on the POST screen.
//ESP_SYSTEM_INIT_FN(esp_hosted_host_init, BIT(0), 120)
static void __attribute__((unused)) esp_hosted_host_init(void)
{
	ESP_LOGI(TAG, "ESP Hosted : Host chip_ip[%d]", CONFIG_IDF_FIRMWARE_CHIP_ID);
	esp_err_t err = esp_hosted_init();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "ESP Hosted init failed: %s", esp_err_to_name(err));
	}
}

static void __attribute__((destructor)) esp_hosted_host_deinit(void)
{
	ESP_LOGI(TAG, "ESP Hosted deinit");
	esp_hosted_deinit();
}
