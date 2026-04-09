/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal ESP32 display binding for ESP32-P4-EYE.
 */

#include "omv_boardconfig.h"

#if MICROPY_PY_DISPLAY && defined(ESP_PLATFORM)

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "py/mphal.h"
#include "py/runtime.h"

#include "fb_alloc.h"
#include "py_display.h"
#include "py_helper.h"

#define OMV_ESP32_LCD_H_RES             (240)
#define OMV_ESP32_LCD_V_RES             (240)
#define OMV_ESP32_LCD_PIXEL_CLOCK_HZ    (80 * 1000 * 1000)
#define OMV_ESP32_LCD_SPI_HOST          (SPI2_HOST)
#define OMV_ESP32_LCD_PIN_MOSI          (GPIO_NUM_16)
#define OMV_ESP32_LCD_PIN_CLK           (GPIO_NUM_17)
#define OMV_ESP32_LCD_PIN_CS            (GPIO_NUM_18)
#define OMV_ESP32_LCD_PIN_DC            (GPIO_NUM_19)
#define OMV_ESP32_LCD_PIN_RST           (GPIO_NUM_15)
#define OMV_ESP32_LCD_PIN_BL            (GPIO_NUM_20)

#define OMV_ESP32_LCD_CMD_BITS          (8)
#define OMV_ESP32_LCD_PARAM_BITS        (8)
#define OMV_ESP32_LCD_BPP               (16)
#define OMV_ESP32_LCD_BRIGHTNESS_CH     (LEDC_CHANNEL_0)
#define OMV_ESP32_LCD_BRIGHTNESS_TIMER  (LEDC_TIMER_0)

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

typedef struct _py_esp32_display_obj_t {
    py_display_obj_t base_obj;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    spi_host_device_t spi_host;
    uint16_t *framebuffer;
} py_esp32_display_obj_t;

static bool omv_esp32_display_brightness_inited = false;

static void omv_esp32_display_brightness_init(void) {
    if (omv_esp32_display_brightness_inited) {
        return;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = OMV_ESP32_LCD_BRIGHTNESS_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel_cfg = {
        .gpio_num = OMV_ESP32_LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = OMV_ESP32_LCD_BRIGHTNESS_CH,
        .timer_sel = OMV_ESP32_LCD_BRIGHTNESS_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags.output_invert = true,
    };

    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display backlight timer init failed"));
    }
    if (ledc_channel_config(&channel_cfg) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display backlight channel init failed"));
    }

    omv_esp32_display_brightness_inited = true;
}

static void omv_esp32_display_set_backlight(py_display_obj_t *self_in, uint32_t intensity) {
    (void) self_in;
    uint32_t duty = (1023U * intensity) / 100U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, OMV_ESP32_LCD_BRIGHTNESS_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, OMV_ESP32_LCD_BRIGHTNESS_CH);
}

static void omv_esp32_display_draw(py_esp32_display_obj_t *self, image_t *src_img, int dst_x_start, int dst_y_start,
                                   float x_scale, float y_scale, rectangle_t *roi, int rgb_channel, int alpha,
                                   const uint16_t *color_palette, const uint8_t *alpha_palette, image_hint_t hint) {
    image_t dst_img = {
        .w = self->base_obj.width,
        .h = self->base_obj.height,
        .pixfmt = PIXFORMAT_RGB565,
        .size = self->base_obj.width * self->base_obj.height * sizeof(uint16_t),
        .data = (uint8_t *) self->framebuffer,
    };

    if (src_img == NULL) {
        memset(dst_img.data, 0, dst_img.size);
    } else {
        imlib_draw_image(&dst_img, src_img, dst_x_start, dst_y_start,
                         x_scale, y_scale, roi, rgb_channel, alpha,
                         color_palette, alpha_palette,
                         hint | IMAGE_HINT_BLACK_BACKGROUND, NULL, NULL, NULL, NULL);
    }

    esp_lcd_panel_draw_bitmap(self->panel_handle, 0, 0, self->base_obj.width, self->base_obj.height, self->framebuffer);
}

static void omv_esp32_display_write(py_display_obj_t *self_in, image_t *src_img, int dst_x_start, int dst_y_start,
                                    float x_scale, float y_scale, rectangle_t *roi, int rgb_channel, int alpha,
                                    const uint16_t *color_palette, const uint8_t *alpha_palette, image_hint_t hint) {
    py_esp32_display_obj_t *self = (py_esp32_display_obj_t *) self_in;
    omv_esp32_display_draw(self, src_img, dst_x_start, dst_y_start, x_scale, y_scale, roi,
                           rgb_channel, alpha, color_palette, alpha_palette, hint);
}

static void omv_esp32_display_clear(py_display_obj_t *self_in, bool display_off) {
    py_esp32_display_obj_t *self = (py_esp32_display_obj_t *) self_in;
    if (display_off) {
        esp_lcd_panel_disp_on_off(self->panel_handle, false);
        self->base_obj.display_on = false;
        return;
    }

    esp_lcd_panel_disp_on_off(self->panel_handle, true);
    self->base_obj.display_on = true;
    omv_esp32_display_draw(self, NULL, 0, 0, 1.0f, 1.0f, NULL, -1, 255, NULL, NULL, 0);
}

static void omv_esp32_display_deinit(py_display_obj_t *self_in) {
    py_esp32_display_obj_t *self = (py_esp32_display_obj_t *) self_in;

    if (self->panel_handle != NULL) {
        esp_lcd_panel_disp_on_off(self->panel_handle, false);
        esp_lcd_panel_del(self->panel_handle);
        self->panel_handle = NULL;
    }
    if (self->io_handle != NULL) {
        esp_lcd_panel_io_del(self->io_handle);
        self->io_handle = NULL;
    }
    if (self->framebuffer != NULL) {
        heap_caps_free(self->framebuffer);
        self->framebuffer = NULL;
    }
    spi_bus_free(self->spi_host);
}

static void omv_esp32_display_init_panel(py_esp32_display_obj_t *self) {
    spi_bus_config_t buscfg = {
        .sclk_io_num = OMV_ESP32_LCD_PIN_CLK,
        .mosi_io_num = OMV_ESP32_LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = self->base_obj.width * 20 * sizeof(uint16_t),
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

    if (spi_bus_initialize(self->spi_host, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display spi init failed"));
    }
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) self->spi_host, &io_config, &self->io_handle) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display panel io init failed"));
    }
    if (esp_lcd_new_panel_st7789(self->io_handle, &panel_config, &self->panel_handle) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display panel init failed"));
    }

    for (size_t i = 0; i < MP_ARRAY_SIZE(omv_esp32_lcd_init_seq); i++) {
        const omv_esp32_lcd_init_cmd_t *cmd = &omv_esp32_lcd_init_seq[i];
        if (esp_lcd_panel_io_tx_param(self->io_handle, cmd->cmd, cmd->data, cmd->data_bytes) != ESP_OK) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display init sequence failed"));
        }
        if (cmd->delay_ms != 0U) {
            mp_hal_delay_ms(cmd->delay_ms);
        }
    }

    esp_lcd_panel_reset(self->panel_handle);
    esp_lcd_panel_init(self->panel_handle);
    esp_lcd_panel_invert_color(self->panel_handle, true);
    esp_lcd_panel_disp_on_off(self->panel_handle, true);
}

static mp_obj_t omv_esp32_display_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum {
        ARG_width,
        ARG_height,
        ARG_refresh,
        ARG_backlight,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_width, MP_ARG_INT, {.u_int = OMV_ESP32_LCD_H_RES} },
        { MP_QSTR_height, MP_ARG_INT, {.u_int = OMV_ESP32_LCD_V_RES} },
        { MP_QSTR_refresh, MP_ARG_INT, {.u_int = 60} },
        { MP_QSTR_backlight, MP_ARG_INT | MP_ARG_KW_ONLY, {.u_int = 100} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if ((args[ARG_width].u_int != OMV_ESP32_LCD_H_RES) || (args[ARG_height].u_int != OMV_ESP32_LCD_V_RES)) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("only 240x240 is supported"));
    }

    py_esp32_display_obj_t *self = mp_obj_malloc_with_finaliser(py_esp32_display_obj_t, &py_esp32_display_type);
    memset(self, 0, sizeof(*self));
    self->base_obj.base.type = &py_esp32_display_type;
    self->base_obj.width = OMV_ESP32_LCD_H_RES;
    self->base_obj.height = OMV_ESP32_LCD_V_RES;
    self->base_obj.framesize = DISPLAY_RESOLUTION_QVGA;
    self->base_obj.refresh = args[ARG_refresh].u_int;
    self->base_obj.intensity = args[ARG_backlight].u_int;
    self->base_obj.display_on = true;
    self->base_obj.triple_buffer = false;
    self->base_obj.bgr = false;
    self->base_obj.byte_swap = false;
    self->base_obj.controller = mp_const_none;
    self->base_obj.bl_controller = mp_const_none;
    self->spi_host = OMV_ESP32_LCD_SPI_HOST;

    self->framebuffer = heap_caps_aligned_alloc(16,
                                                self->base_obj.width * self->base_obj.height * sizeof(uint16_t),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (self->framebuffer == NULL) {
        self->framebuffer = heap_caps_aligned_alloc(16,
                                                    self->base_obj.width * self->base_obj.height * sizeof(uint16_t),
                                                    MALLOC_CAP_8BIT);
    }
    if (self->framebuffer == NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("display framebuffer alloc failed"));
    }
    memset(self->framebuffer, 0, self->base_obj.width * self->base_obj.height * sizeof(uint16_t));

    omv_esp32_display_brightness_init();
    omv_esp32_display_init_panel(self);
    omv_esp32_display_set_backlight(&self->base_obj, self->base_obj.intensity);
    omv_esp32_display_draw(self, NULL, 0, 0, 1.0f, 1.0f, NULL, -1, 255, NULL, NULL, 0);

    return MP_OBJ_FROM_PTR(self);
}

static const py_display_p_t py_esp32_display_p = {
    .deinit = omv_esp32_display_deinit,
    .clear = omv_esp32_display_clear,
    .write = omv_esp32_display_write,
    .set_backlight = omv_esp32_display_set_backlight,
};

MP_DEFINE_CONST_OBJ_TYPE(
    py_esp32_display_type,
    MP_QSTR_ESP32Display,
    MP_TYPE_FLAG_NONE,
    make_new, omv_esp32_display_make_new,
    protocol, &py_esp32_display_p,
    locals_dict, &py_display_locals_dict
    );

#endif // MICROPY_PY_DISPLAY && defined(ESP_PLATFORM)
