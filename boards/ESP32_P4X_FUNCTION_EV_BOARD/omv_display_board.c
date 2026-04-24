#include "omv_board.h"

esp_err_t omv_esp32_board_display_brightness_init(void) {
    return ESP_ERR_NOT_SUPPORTED;
}

void omv_esp32_board_display_set_backlight(uint32_t intensity) {
    (void) intensity;
}

esp_err_t omv_esp32_board_display_init_panel(int width, int height,
                                             esp_lcd_panel_io_handle_t *io_handle,
                                             esp_lcd_panel_handle_t *panel_handle) {
    (void) width;
    (void) height;
    if (io_handle != NULL) {
        *io_handle = NULL;
    }
    if (panel_handle != NULL) {
        *panel_handle = NULL;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

void omv_esp32_board_display_deinit_panel(esp_lcd_panel_io_handle_t io_handle,
                                           esp_lcd_panel_handle_t panel_handle) {
    (void) io_handle;
    (void) panel_handle;
}
