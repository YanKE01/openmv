#include "omv_sdcard.h"

#include "driver/gpio.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#define OMV_ESP32_SD_EN_PIN       (GPIO_NUM_46)
#define OMV_ESP32_SD_DETECT_PIN   (GPIO_NUM_45)
#define OMV_ESP32_SD_LDO_CHAN_ID  (4)

static sd_pwr_ctrl_handle_t omv_esp32_sd_ldo_handle = NULL;

void omv_esp32_sdcard_init0(void) {
    const gpio_config_t sd_en_config = {
        .pin_bit_mask = BIT64(OMV_ESP32_SD_EN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sd_en_config);
    gpio_set_level(OMV_ESP32_SD_EN_PIN, 0);

    const gpio_config_t sd_detect_config = {
        .pin_bit_mask = BIT64(OMV_ESP32_SD_DETECT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sd_detect_config);
}

esp_err_t omv_esp32_sdcard_preinit_host(sdmmc_host_t *host, int slot) {
    if (host == NULL || slot != 0) {
        return ESP_OK;
    }

    int detect = gpio_get_level(OMV_ESP32_SD_DETECT_PIN);
    if (detect != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (omv_esp32_sd_ldo_handle != NULL) {
        host->pwr_ctrl_handle = omv_esp32_sd_ldo_handle;
        return ESP_OK;
    }

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = OMV_ESP32_SD_LDO_CHAN_ID,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret == ESP_OK) {
        omv_esp32_sd_ldo_handle = pwr_ctrl_handle;
        host->pwr_ctrl_handle = pwr_ctrl_handle;
    }
    return ret;
}

void omv_esp32_sdcard_deinit_host(sdmmc_host_t *host, int slot) {
    if (host == NULL || slot != 0 || host->pwr_ctrl_handle == NULL) {
        return;
    }

    host->pwr_ctrl_handle = NULL;
}
