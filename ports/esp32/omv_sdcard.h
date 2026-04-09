#ifndef OMV_ESP32_SDCARD_H
#define OMV_ESP32_SDCARD_H

#include "esp_err.h"
#include "driver/sdmmc_host.h"

void omv_esp32_sdcard_init0(void);
esp_err_t omv_esp32_sdcard_preinit_host(sdmmc_host_t *host, int slot);
void omv_esp32_sdcard_deinit_host(sdmmc_host_t *host, int slot);

#endif // OMV_ESP32_SDCARD_H
