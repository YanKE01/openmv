/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal ESP32 display binding for ESP32-P4-EYE.
 */

#include "omv_boardconfig.h"

#if MICROPY_PY_DISPLAY && defined(ESP_PLATFORM)

#include <string.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "py/runtime.h"

#include "fb_alloc.h"
#include "omv_board.h"
#include "py_display.h"
#include "py_helper.h"

typedef struct _py_esp32_display_obj_t {
    py_display_obj_t base_obj;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    spi_host_device_t spi_host;
    uint16_t *framebuffer;
} py_esp32_display_obj_t;

static void omv_esp32_display_set_backlight(py_display_obj_t *self_in, uint32_t intensity) {
    (void) self_in;
    omv_esp32_board_display_set_backlight(intensity);
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
    self->spi_host = (spi_host_device_t) OMV_ESP32_LCD_SPI_HOST;

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

    if (omv_esp32_board_display_brightness_init() != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display backlight init failed"));
    }
    if (omv_esp32_board_display_init_panel(self->spi_host, self->base_obj.width, self->base_obj.height,
                                           &self->io_handle, &self->panel_handle) != ESP_OK) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("display panel init failed"));
    }
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
