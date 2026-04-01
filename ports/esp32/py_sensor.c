/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal sensor module for the ESP32 OpenMV bring-up path.
 */

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/objmodule.h"

#include "framebuffer.h"
#include "imlib.h"
#include "omv_camera.h"
#include "omv_csi.h"
#include "py_image.h"

static framebuffer_t *py_sensor_get_mainfb(void) {
    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);

    if ((fb == NULL) || (fb->raw_base == NULL)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("framebuffer unavailable"));
    }

    return fb;
}

static void py_sensor_ensure_camera_ready(void) {
    if (omv_esp32_camera_is_ready()) {
        return;
    }

    if (omv_esp32_camera_init() != 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("camera init failed"));
    }
}

static mp_obj_t py_sensor_reset(void) {
    py_sensor_ensure_camera_ready();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_reset_obj, py_sensor_reset);

static mp_obj_t py_sensor_set_pixformat(mp_obj_t pixformat_in) {
    if (mp_obj_get_int(pixformat_in) != PIXFORMAT_RGB565) {
        mp_raise_ValueError(MP_ERROR_TEXT("only RGB565 is supported"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_pixformat_obj, py_sensor_set_pixformat);

static mp_obj_t py_sensor_set_framesize(mp_obj_t framesize_in) {
    if (mp_obj_get_int(framesize_in) != OMV_CSI_FRAMESIZE_QVGA) {
        mp_raise_ValueError(MP_ERROR_TEXT("only QVGA is supported"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_framesize_obj, py_sensor_set_framesize);

static mp_obj_t py_sensor_snapshot(void) {
    framebuffer_t *fb = py_sensor_get_mainfb();
    size_t pixel_count = fb->raw_size / sizeof(uint16_t);

    py_sensor_ensure_camera_ready();

    if (!omv_esp32_camera_capture_rgb565((uint16_t *) fb->raw_base, pixel_count)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("snapshot failed"));
    }

    fb->w = omv_esp32_camera_get_width();
    fb->h = omv_esp32_camera_get_height();
    fb->pixfmt = PIXFORMAT_RGB565;
    fb->size = (size_t) fb->w * (size_t) fb->h * sizeof(uint16_t);

    image_t image;
    framebuffer_to_image(fb, &image);
    framebuffer_update_preview(&image);
    return py_image_from_struct(&image);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_snapshot_obj, py_sensor_snapshot);

static mp_obj_t py_sensor_get_id(void) {
    return mp_obj_new_int_from_uint(omv_esp32_camera_get_id());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_id_obj, py_sensor_get_id);

static mp_obj_t py_sensor_set_hmirror(mp_obj_t enable_in) {
    py_sensor_ensure_camera_ready();

    if (!omv_esp32_camera_set_hmirror(mp_obj_is_true(enable_in))) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("set_hmirror failed"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_hmirror_obj, py_sensor_set_hmirror);

static mp_obj_t py_sensor_get_hmirror(void) {
    return mp_obj_new_bool(omv_esp32_camera_get_hmirror());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_hmirror_obj, py_sensor_get_hmirror);

static mp_obj_t py_sensor_set_vflip(mp_obj_t enable_in) {
    py_sensor_ensure_camera_ready();

    if (!omv_esp32_camera_set_vflip(mp_obj_is_true(enable_in))) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("set_vflip failed"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_vflip_obj, py_sensor_set_vflip);

static mp_obj_t py_sensor_get_vflip(void) {
    return mp_obj_new_bool(omv_esp32_camera_get_vflip());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_vflip_obj, py_sensor_get_vflip);

static mp_obj_t py_sensor_width(void) {
    py_sensor_ensure_camera_ready();
    return mp_obj_new_int_from_uint(omv_esp32_camera_get_width());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_width_obj, py_sensor_width);

static mp_obj_t py_sensor_height(void) {
    py_sensor_ensure_camera_ready();
    return mp_obj_new_int_from_uint(omv_esp32_camera_get_height());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_height_obj, py_sensor_height);

static mp_obj_t py_sensor_skip_frames(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum {
        ARG_n,
        ARG_time,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_n, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_time, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    framebuffer_t *fb = py_sensor_get_mainfb();
    size_t pixel_count = fb->raw_size / sizeof(uint16_t);
    uint32_t n = args[ARG_n].u_int;
    uint32_t duration_ms = args[ARG_time].u_int;
    uint32_t end_ms = mp_hal_ticks_ms() + duration_ms;

    py_sensor_ensure_camera_ready();

    if ((n == 0) && (duration_ms == 0)) {
        n = 1;
    }

    while ((n > 0) || (duration_ms && ((int32_t) (mp_hal_ticks_ms() - end_ms) < 0))) {
        if (!omv_esp32_camera_capture_rgb565((uint16_t *) fb->raw_base, pixel_count)) {
            mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("skip_frames failed"));
        }

        if (n > 0) {
            n -= 1;
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_sensor_skip_frames_obj, 0, py_sensor_skip_frames);

static const mp_rom_map_elem_t sensor_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sensor) },
    { MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(PIXFORMAT_RGB565) },
    { MP_ROM_QSTR(MP_QSTR_QVGA), MP_ROM_INT(OMV_CSI_FRAMESIZE_QVGA) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&py_sensor_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pixformat), MP_ROM_PTR(&py_sensor_set_pixformat_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_framesize), MP_ROM_PTR(&py_sensor_set_framesize_obj) },
    { MP_ROM_QSTR(MP_QSTR_snapshot), MP_ROM_PTR(&py_sensor_snapshot_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_id), MP_ROM_PTR(&py_sensor_get_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_skip_frames), MP_ROM_PTR(&py_sensor_skip_frames_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_hmirror), MP_ROM_PTR(&py_sensor_set_hmirror_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_hmirror), MP_ROM_PTR(&py_sensor_get_hmirror_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_vflip), MP_ROM_PTR(&py_sensor_set_vflip_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_vflip), MP_ROM_PTR(&py_sensor_get_vflip_obj) },
    { MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&py_sensor_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&py_sensor_height_obj) },
};
static MP_DEFINE_CONST_DICT(sensor_module_globals, sensor_module_globals_table);

const mp_obj_module_t sensor_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *) &sensor_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_sensor, sensor_module);
