#include "omv_board.h"
#include "omv_boardconfig.h"

#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

static sd_pwr_ctrl_handle_t omv_esp32_sd_ldo_handle = NULL;

void omv_esp32_board_sdcard_init0(void) {
}

esp_err_t omv_esp32_board_sdcard_preinit_host(sdmmc_host_t *host, int slot) {
    if ((host == NULL) || (slot != OMV_ESP32_SDMMC_SLOT)) {
        return ESP_OK;
    }

    host->slot = OMV_ESP32_SDMMC_SLOT;
    host->max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    if (omv_esp32_sd_ldo_handle != NULL) {
        host->pwr_ctrl_handle = omv_esp32_sd_ldo_handle;
        return ESP_OK;
    }

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = OMV_ESP32_SD_LDO_CHAN_ID,
    };
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &omv_esp32_sd_ldo_handle);
    if (ret == ESP_OK) {
        host->pwr_ctrl_handle = omv_esp32_sd_ldo_handle;
    }
    return ret;
}

void omv_esp32_board_sdcard_deinit_host(sdmmc_host_t *host, int slot) {
    (void) host;
    (void) slot;
}
