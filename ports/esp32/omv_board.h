#ifndef OMV_ESP32_BOARD_H
#define OMV_ESP32_BOARD_H

#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

void omv_esp32_board_sdcard_init0(void);
esp_err_t omv_esp32_board_sdcard_preinit_host(sdmmc_host_t *host, int slot);
void omv_esp32_board_sdcard_deinit_host(sdmmc_host_t *host, int slot);

esp_err_t omv_esp32_board_display_brightness_init(void);
void omv_esp32_board_display_set_backlight(uint32_t intensity);
esp_err_t omv_esp32_board_display_init_panel(spi_host_device_t spi_host, int width, int height,
                                             esp_lcd_panel_io_handle_t *io_handle,
                                             esp_lcd_panel_handle_t *panel_handle);

int omv_esp32_board_camera_start_xclk(esp_cam_sensor_xclk_handle_t *xclk_handle);
void omv_esp32_board_camera_stop_xclk(esp_cam_sensor_xclk_handle_t xclk_handle);
int omv_esp32_board_camera_video_init(void);
void omv_esp32_board_camera_video_deinit(void);
int omv_esp32_board_camera_open_device(int *fd);
int omv_esp32_board_camera_set_format(int fd, uint32_t *input_width, uint32_t *input_height, uint32_t *pixfmt);
uint32_t omv_esp32_board_camera_get_sensor_id(void);

#endif // OMV_ESP32_BOARD_H
