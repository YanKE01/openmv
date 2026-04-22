#include "omv_sdcard.h"
#include "omv_board.h"

void omv_esp32_sdcard_init0(void) {
    omv_esp32_board_sdcard_init0();
}

esp_err_t omv_esp32_sdcard_preinit_host(sdmmc_host_t *host, int slot) {
    return omv_esp32_board_sdcard_preinit_host(host, slot);
}

void omv_esp32_sdcard_deinit_host(sdmmc_host_t *host, int slot) {
    omv_esp32_board_sdcard_deinit_host(host, slot);
}
