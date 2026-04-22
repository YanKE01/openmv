#include "omv_boardconfig.h"
#include "omv_board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    int cmd;
    const uint8_t data[16];
    size_t data_bytes;
    uint32_t delay_ms;
} omv_esp32_lcd_init_cmd_t;

static const omv_esp32_lcd_init_cmd_t omv_esp32_lcd_init_seq[] = {
    {0x11, {0x00}, 1, 120},
    {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5, 0},
    {0x35, {0x00}, 1, 0},
    {0x36, {0x00}, 1, 0},
    {0x3A, {0x05}, 1, 0},
    {0xB7, {0x35}, 1, 0},
    {0xBB, {0x2D}, 1, 0},
    {0xC0, {0x2C}, 1, 0},
    {0xC2, {0x01}, 1, 0},
    {0xC3, {0x15}, 1, 0},
    {0xC4, {0x20}, 1, 0},
    {0xC6, {0x0F}, 1, 0},
    {0xD0, {0xA4, 0xA1}, 2, 0},
    {0xD6, {0xA1}, 1, 0},
    {0xE0, {0x70, 0x05, 0x0A, 0x0B, 0x0A, 0x27, 0x2F, 0x44, 0x47, 0x37, 0x14, 0x14, 0x29, 0x2F}, 14, 0},
    {0xE1, {0x70, 0x07, 0x0C, 0x08, 0x08, 0x04, 0x2F, 0x33, 0x46, 0x18, 0x15, 0x15, 0x2B, 0x2D}, 14, 0},
    {0x21, {0x00}, 1, 0},
    {0x29, {0x00}, 1, 0},
    {0x2C, {0x00}, 1, 0},
};

static bool omv_esp32_display_brightness_inited = false;

esp_err_t omv_esp32_board_display_brightness_init(void) {
    if (omv_esp32_display_brightness_inited) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = (ledc_timer_t) OMV_ESP32_LCD_BRIGHTNESS_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel_cfg = {
        .gpio_num = OMV_ESP32_LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = (ledc_channel_t) OMV_ESP32_LCD_BRIGHTNESS_CH,
        .timer_sel = (ledc_timer_t) OMV_ESP32_LCD_BRIGHTNESS_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = true,
    };

    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = ledc_channel_config(&channel_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    omv_esp32_display_brightness_inited = true;
    return ESP_OK;
}

void omv_esp32_board_display_set_backlight(uint32_t intensity) {
    uint32_t duty = (1023U * intensity) / 100U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t) OMV_ESP32_LCD_BRIGHTNESS_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t) OMV_ESP32_LCD_BRIGHTNESS_CH);
}

esp_err_t omv_esp32_board_display_init_panel(spi_host_device_t spi_host, int width, int height,
                                             esp_lcd_panel_io_handle_t *io_handle,
                                             esp_lcd_panel_handle_t *panel_handle) {
    spi_bus_config_t buscfg = {
        .sclk_io_num = OMV_ESP32_LCD_PIN_CLK,
        .mosi_io_num = OMV_ESP32_LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = width * 20 * (OMV_ESP32_LCD_BPP / 8),
    };
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = OMV_ESP32_LCD_PIN_DC,
        .cs_gpio_num = OMV_ESP32_LCD_PIN_CS,
        .pclk_hz = OMV_ESP32_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = OMV_ESP32_LCD_CMD_BITS,
        .lcd_param_bits = OMV_ESP32_LCD_PARAM_BITS,
        .spi_mode = 3,
        .trans_queue_depth = 2,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = OMV_ESP32_LCD_PIN_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = OMV_ESP32_LCD_BPP,
    };

    esp_err_t ret = spi_bus_initialize(spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) spi_host, &io_config, io_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_new_panel_st7789(*io_handle, &panel_config, panel_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    for (size_t i = 0; i < (sizeof(omv_esp32_lcd_init_seq) / sizeof(omv_esp32_lcd_init_seq[0])); i++) {
        const omv_esp32_lcd_init_cmd_t *cmd = &omv_esp32_lcd_init_seq[i];
        ret = esp_lcd_panel_io_tx_param(*io_handle, cmd->cmd, cmd->data, cmd->data_bytes);
        if (ret != ESP_OK) {
            return ret;
        }
        if (cmd->delay_ms != 0U) {
            vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        }
    }

    ret = esp_lcd_panel_reset(*panel_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_init(*panel_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = esp_lcd_panel_invert_color(*panel_handle, true);
    if (ret != ESP_OK) {
        return ret;
    }
    return esp_lcd_panel_disp_on_off(*panel_handle, true);
}
