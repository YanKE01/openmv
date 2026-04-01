/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal Image object for the ESP32 OpenMV bring-up path.
 */

#include <math.h>

#include "py/objmodule.h"
#include "py/runtime.h"

#include "framebuffer.h"
#include "imlib.h"
#include "omv_bar.h"
#include "omv_qr.h"
#include "omv_imlib_blob_min.h"
#include "omv_imlib_edge_min.h"
#include "omv_imlib_filter_min.h"
#include "omv_imlib_gray_min.h"
#include "omv_imlib_shape_min.h"

#include "py_image_lite.h"

typedef struct _py_esp32_image_obj_t {
    mp_obj_base_t base;
} py_esp32_image_obj_t;

typedef struct _py_barcode_obj_t {
    mp_obj_base_t base;
    mp_obj_t corners;
    mp_obj_t x;
    mp_obj_t y;
    mp_obj_t w;
    mp_obj_t h;
    mp_obj_t payload;
    mp_obj_t type;
    mp_obj_t rotation;
    mp_obj_t quality;
} py_barcode_obj_t;

typedef struct _py_qrcode_obj_t {
    mp_obj_base_t base;
    mp_obj_t corners;
    mp_obj_t x;
    mp_obj_t y;
    mp_obj_t w;
    mp_obj_t h;
    mp_obj_t payload;
    mp_obj_t version;
    mp_obj_t ecc_level;
    mp_obj_t mask;
    mp_obj_t data_type;
    mp_obj_t eci;
} py_qrcode_obj_t;

typedef struct _py_esp32_blob_obj_t {
    mp_obj_base_t base;
    mp_obj_t x;
    mp_obj_t y;
    mp_obj_t w;
    mp_obj_t h;
    mp_obj_t pixels;
    mp_obj_t cx;
    mp_obj_t cy;
    mp_obj_t code;
    mp_obj_t count;
} py_esp32_blob_obj_t;

typedef struct _py_esp32_circle_obj_t {
    mp_obj_base_t base;
    mp_obj_t x;
    mp_obj_t y;
    mp_obj_t r;
    mp_obj_t magnitude;
} py_esp32_circle_obj_t;

typedef struct _py_esp32_rect_obj_t {
    mp_obj_base_t base;
    mp_obj_t corners;
    mp_obj_t rect;
    mp_obj_t magnitude;
} py_esp32_rect_obj_t;

static py_esp32_image_obj_t py_esp32_image_obj;
static void py_barcode_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest);
static void py_qrcode_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest);

static framebuffer_t *py_esp32_image_fb(void) {
    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);

    if ((fb == NULL) || (fb->raw_base == NULL)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("framebuffer unavailable"));
    }

    return fb;
}

static void py_esp32_image_to_cobj(image_t *image) {
    framebuffer_to_image(py_esp32_image_fb(), image);
}

static void py_esp32_image_refresh_preview(image_t *image) {
    framebuffer_update_preview(image);
}

static void py_esp32_image_mark_dirty(void) {
    omv_esp32_imlib_gray_mark_dirty();
}

static int py_esp32_parse_color(mp_obj_t color_in) {
    if (mp_obj_is_int(color_in)) {
        return mp_obj_get_int(color_in);
    }

    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(color_in, &len, &items);
    if (len != 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("color must be int or (r,g,b)"));
    }

    int r = mp_obj_get_int(items[0]);
    int g = mp_obj_get_int(items[1]);
    int b = mp_obj_get_int(items[2]);

    if ((r < 0) || (r > 255) || (g < 0) || (g > 255) || (b < 0) || (b > 255)) {
        mp_raise_ValueError(MP_ERROR_TEXT("RGB values must be 0-255"));
    }

    return COLOR_R8_G8_B8_TO_RGB565(r, g, b);
}

static void py_esp32_parse_fixed_int_tuple(mp_obj_t tuple_in, size_t expected_len, int *out) {
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(tuple_in, &len, &items);
    if (len != expected_len) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid tuple size"));
    }

    for (size_t i = 0; i < expected_len; i++) {
        out[i] = mp_obj_get_int(items[i]);
    }
}

static void py_esp32_parse_binary_threshold(mp_obj_t thresholds_in, int *lo, int *hi) {
    size_t len;
    mp_obj_t *items;

    mp_obj_get_array(thresholds_in, &len, &items);

    if ((len == 2) && mp_obj_is_int(items[0]) && mp_obj_is_int(items[1])) {
        *lo = mp_obj_get_int(items[0]);
        *hi = mp_obj_get_int(items[1]);
    } else if (len == 1) {
        size_t inner_len;
        mp_obj_t *inner_items;

        mp_obj_get_array(items[0], &inner_len, &inner_items);
        if (inner_len != 2) {
            mp_raise_ValueError(MP_ERROR_TEXT("threshold must be (lo,hi)"));
        }
        *lo = mp_obj_get_int(inner_items[0]);
        *hi = mp_obj_get_int(inner_items[1]);
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("threshold must be (lo,hi)"));
    }

    if ((*lo < 0) || (*lo > 255) || (*hi < 0) || (*hi > 255) || (*lo > *hi)) {
        mp_raise_ValueError(MP_ERROR_TEXT("threshold must satisfy 0<=lo<=hi<=255"));
    }
}

static int py_esp32_parse_ksize(mp_obj_t ksize_in) {
    int ksize = mp_obj_get_int(ksize_in);

    if (ksize < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("ksize must be >= 0"));
    }

    return ksize;
}

static void py_esp32_fill_roi(image_t *image, mp_obj_t roi_in, rectangle_t *roi) {
    int rect[4];

    if ((roi_in == MP_OBJ_NULL) || (roi_in == mp_const_none)) {
        roi->x = 0;
        roi->y = 0;
        roi->w = image->w;
        roi->h = image->h;
        return;
    }

    py_esp32_parse_fixed_int_tuple(roi_in, 4, rect);
    roi->x = rect[0];
    roi->y = rect[1];
    roi->w = rect[2];
    roi->h = rect[3];

    if ((roi->w <= 0) || (roi->h <= 0)) {
        mp_raise_ValueError(MP_ERROR_TEXT("roi size must be > 0"));
    }
}

static void py_esp32_threshold_sort_pair(int *lo, int *hi) {
    if (*lo > *hi) {
        int tmp = *lo;
        *lo = *hi;
        *hi = tmp;
    }
}

static size_t py_esp32_parse_blob_thresholds(mp_obj_t thresholds_in, omv_esp32_threshold_t **out) {
    size_t len;
    mp_obj_t *items;

    mp_obj_get_array(thresholds_in, &len, &items);

    bool single_tuple = ((len == 2) && mp_obj_is_int(items[0]) && mp_obj_is_int(items[1])) || (len == 6);
    if (single_tuple) {
        omv_esp32_threshold_t *thresholds = m_new(omv_esp32_threshold_t, 1);

        if (len == 2) {
            thresholds[0].mode = OMV_ESP32_THRESHOLD_MODE_Y;
            thresholds[0].l_lo = mp_obj_get_int(items[0]);
            thresholds[0].l_hi = mp_obj_get_int(items[1]);
            py_esp32_threshold_sort_pair(&thresholds[0].l_lo, &thresholds[0].l_hi);
            thresholds[0].a_lo = 0;
            thresholds[0].a_hi = 0;
            thresholds[0].b_lo = 0;
            thresholds[0].b_hi = 0;
        } else {
            thresholds[0].mode = OMV_ESP32_THRESHOLD_MODE_LAB;
            thresholds[0].l_lo = mp_obj_get_int(items[0]);
            thresholds[0].l_hi = mp_obj_get_int(items[1]);
            thresholds[0].a_lo = mp_obj_get_int(items[2]);
            thresholds[0].a_hi = mp_obj_get_int(items[3]);
            thresholds[0].b_lo = mp_obj_get_int(items[4]);
            thresholds[0].b_hi = mp_obj_get_int(items[5]);
            py_esp32_threshold_sort_pair(&thresholds[0].l_lo, &thresholds[0].l_hi);
            py_esp32_threshold_sort_pair(&thresholds[0].a_lo, &thresholds[0].a_hi);
            py_esp32_threshold_sort_pair(&thresholds[0].b_lo, &thresholds[0].b_hi);
        }

        *out = thresholds;
        return 1;
    }

    omv_esp32_threshold_t *thresholds = m_new(omv_esp32_threshold_t, len);
    for (size_t i = 0; i < len; i++) {
        size_t inner_len;
        mp_obj_t *inner_items;

        mp_obj_get_array(items[i], &inner_len, &inner_items);
        if (inner_len == 2) {
            thresholds[i].mode = OMV_ESP32_THRESHOLD_MODE_Y;
            thresholds[i].l_lo = mp_obj_get_int(inner_items[0]);
            thresholds[i].l_hi = mp_obj_get_int(inner_items[1]);
            py_esp32_threshold_sort_pair(&thresholds[i].l_lo, &thresholds[i].l_hi);
            thresholds[i].a_lo = 0;
            thresholds[i].a_hi = 0;
            thresholds[i].b_lo = 0;
            thresholds[i].b_hi = 0;
        } else if (inner_len == 6) {
            thresholds[i].mode = OMV_ESP32_THRESHOLD_MODE_LAB;
            thresholds[i].l_lo = mp_obj_get_int(inner_items[0]);
            thresholds[i].l_hi = mp_obj_get_int(inner_items[1]);
            thresholds[i].a_lo = mp_obj_get_int(inner_items[2]);
            thresholds[i].a_hi = mp_obj_get_int(inner_items[3]);
            thresholds[i].b_lo = mp_obj_get_int(inner_items[4]);
            thresholds[i].b_hi = mp_obj_get_int(inner_items[5]);
            py_esp32_threshold_sort_pair(&thresholds[i].l_lo, &thresholds[i].l_hi);
            py_esp32_threshold_sort_pair(&thresholds[i].a_lo, &thresholds[i].a_hi);
            py_esp32_threshold_sort_pair(&thresholds[i].b_lo, &thresholds[i].b_hi);
        } else {
            m_del(omv_esp32_threshold_t, thresholds, len);
            mp_raise_ValueError(MP_ERROR_TEXT("threshold must be (lo,hi) or (l_lo,l_hi,a_lo,a_hi,b_lo,b_hi)"));
        }
    }

    *out = thresholds;
    return len;
}

static void py_esp32_free_blob_thresholds(omv_esp32_threshold_t *thresholds, size_t len) {
    m_del(omv_esp32_threshold_t, thresholds, len);
}

static mp_obj_t py_esp32_image_width(mp_obj_t self_in) {
    (void) self_in;
    return mp_obj_new_int(py_esp32_image_fb()->w);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_width_obj, py_esp32_image_width);

static mp_obj_t py_esp32_image_height(mp_obj_t self_in) {
    (void) self_in;
    return mp_obj_new_int(py_esp32_image_fb()->h);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_height_obj, py_esp32_image_height);

static mp_obj_t py_esp32_image_format(mp_obj_t self_in) {
    (void) self_in;
    return mp_obj_new_int_from_uint(py_esp32_image_fb()->pixfmt);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_format_obj, py_esp32_image_format);

static mp_obj_t py_esp32_image_size(mp_obj_t self_in) {
    (void) self_in;
    return mp_obj_new_int_from_uint(py_esp32_image_fb()->size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_size_obj, py_esp32_image_size);

static mp_obj_t py_esp32_image_flush(mp_obj_t self_in) {
    image_t image;

    py_esp32_image_to_cobj(&image);
    py_esp32_image_refresh_preview(&image);
    return self_in;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_flush_obj, py_esp32_image_flush);

static mp_obj_t py_esp32_image_draw_line(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_thickness,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(COLOR_R8_G8_B8_TO_RGB565(255, 255, 255))} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    int line[4];
    image_t image;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_parse_fixed_int_tuple(args[1], 4, line);
    py_esp32_image_to_cobj(&image);
    imlib_draw_line(&image,
                    line[0],
                    line[1],
                    line[2],
                    line[3],
                    py_esp32_parse_color(parsed[ARG_color].u_obj),
                    parsed[ARG_thickness].u_int);
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_line_obj, 2, py_esp32_image_draw_line);

static mp_obj_t py_esp32_image_draw_rectangle(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_thickness,
        ARG_fill,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(COLOR_R8_G8_B8_TO_RGB565(255, 255, 255))} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_fill, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    int rect[4];
    image_t image;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_parse_fixed_int_tuple(args[1], 4, rect);
    py_esp32_image_to_cobj(&image);
    imlib_draw_rectangle(&image,
                         rect[0],
                         rect[1],
                         rect[2],
                         rect[3],
                         py_esp32_parse_color(parsed[ARG_color].u_obj),
                         parsed[ARG_thickness].u_int,
                         parsed[ARG_fill].u_bool);
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_rectangle_obj, 2, py_esp32_image_draw_rectangle);

static mp_obj_t py_esp32_image_draw_circle(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_thickness,
        ARG_fill,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(COLOR_R8_G8_B8_TO_RGB565(255, 255, 255))} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_fill, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    int circle[3];
    image_t image;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_parse_fixed_int_tuple(args[1], 3, circle);
    py_esp32_image_to_cobj(&image);
    imlib_draw_circle(&image,
                      circle[0],
                      circle[1],
                      circle[2],
                      py_esp32_parse_color(parsed[ARG_color].u_obj),
                      parsed[ARG_thickness].u_int,
                      parsed[ARG_fill].u_bool);
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_circle_obj, 2, py_esp32_image_draw_circle);

static mp_obj_t py_esp32_image_draw_cross(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_size,
        ARG_thickness,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(COLOR_R8_G8_B8_TO_RGB565(255, 255, 255))} },
        { MP_QSTR_size, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 5} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    int point[2];
    int color;
    image_t image;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_parse_fixed_int_tuple(args[1], 2, point);
    color = py_esp32_parse_color(parsed[ARG_color].u_obj);
    py_esp32_image_to_cobj(&image);
    imlib_draw_line(&image,
                    point[0] - parsed[ARG_size].u_int,
                    point[1],
                    point[0] + parsed[ARG_size].u_int,
                    point[1],
                    color,
                    parsed[ARG_thickness].u_int);
    imlib_draw_line(&image,
                    point[0],
                    point[1] - parsed[ARG_size].u_int,
                    point[0],
                    point[1] + parsed[ARG_size].u_int,
                    color,
                    parsed[ARG_thickness].u_int);
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_cross_obj, 2, py_esp32_image_draw_cross);

static mp_obj_t py_esp32_image_draw_string(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_scale,
        ARG_x_spacing,
        ARG_y_spacing,
        ARG_mono_space,
        ARG_char_rotation,
        ARG_char_hmirror,
        ARG_char_vflip,
        ARG_string_rotation,
        ARG_string_hmirror,
        ARG_string_vflip,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(COLOR_R8_G8_B8_TO_RGB565(255, 255, 255))} },
        { MP_QSTR_scale, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_INT(1)} },
        { MP_QSTR_x_spacing, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_y_spacing, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_mono_space, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
        { MP_QSTR_char_rotation, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_char_hmirror, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_char_vflip, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_string_rotation, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_string_hmirror, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_string_vflip, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    size_t len;
    mp_obj_t *items;
    image_t image;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    mp_obj_get_array(args[1], &len, &items);
    if (len != 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("expected (x, y, string)"));
    }

    py_esp32_image_to_cobj(&image);
    imlib_draw_string(&image,
                      mp_obj_get_int(items[0]),
                      mp_obj_get_int(items[1]),
                      mp_obj_str_get_str(items[2]),
                      py_esp32_parse_color(parsed[ARG_color].u_obj),
                      mp_obj_get_float(parsed[ARG_scale].u_obj),
                      parsed[ARG_x_spacing].u_int,
                      parsed[ARG_y_spacing].u_int,
                      parsed[ARG_mono_space].u_bool,
                      parsed[ARG_char_rotation].u_int,
                      parsed[ARG_char_hmirror].u_bool,
                      parsed[ARG_char_vflip].u_bool,
                      parsed[ARG_string_rotation].u_int,
                      parsed[ARG_string_hmirror].u_bool,
                      parsed[ARG_string_vflip].u_bool);
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_string_obj, 2, py_esp32_image_draw_string);

static mp_obj_t py_esp32_image_binary(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_thresholds,
        ARG_invert,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_thresholds, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_invert, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;
    int lo = 0;
    int hi = 255;

    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_image_to_cobj(&image);

    if (image.pixfmt != PIXFORMAT_RGB565) {
        mp_raise_ValueError(MP_ERROR_TEXT("binary only supports RGB565"));
    }

    py_esp32_parse_binary_threshold(parsed[ARG_thresholds].u_obj, &lo, &hi);

    if (!omv_esp32_imlib_binary(&image, lo, hi, parsed[ARG_invert].u_bool)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("binary failed"));
    }

    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_binary_obj, 2, py_esp32_image_binary);

static mp_obj_t py_esp32_image_invert(mp_obj_t self_in) {
    image_t image;

    py_esp32_image_to_cobj(&image);
    if (!omv_esp32_imlib_invert(&image)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invert failed"));
    }
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return self_in;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_invert_obj, py_esp32_image_invert);

static mp_obj_t py_esp32_image_find_edges(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_threshold,
        ARG_roi,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_threshold, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_roi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    int thresh[2] = {100, 200};
    rectangle_t roi;
    image_t image;
    bool ok = false;

    if (n_args < 2) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected edge type"));
    }

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_image_to_cobj(&image);

    roi.x = 0;
    roi.y = 0;
    roi.w = image.w;
    roi.h = image.h;

    if (parsed[ARG_threshold].u_obj != mp_const_none) {
        mp_obj_t *thresh_items;
        mp_obj_get_array_fixed_n(parsed[ARG_threshold].u_obj, 2, &thresh_items);
        thresh[0] = mp_obj_get_int(thresh_items[0]);
        thresh[1] = mp_obj_get_int(thresh_items[1]);
    }

    if (parsed[ARG_roi].u_obj != mp_const_none) {
        int rect[4];
        py_esp32_parse_fixed_int_tuple(parsed[ARG_roi].u_obj, 4, rect);
        roi.x = rect[0];
        roi.y = rect[1];
        roi.w = rect[2];
        roi.h = rect[3];
    }

    switch (mp_obj_get_int(args[1])) {
        case EDGE_SIMPLE:
            ok = omv_esp32_imlib_edge_simple(&image, &roi, thresh[0], thresh[1]);
            break;
        case EDGE_CANNY:
            ok = omv_esp32_imlib_edge_canny(&image, &roi, thresh[0], thresh[1]);
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported edge type"));
    }

    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("find_edges failed"));
    }

    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_find_edges_obj, 2, py_esp32_image_find_edges);

static mp_obj_t py_esp32_image_mean(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_threshold,
        ARG_offset,
        ARG_invert,
        ARG_mask,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_threshold, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_offset, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_invert, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_mask, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    if (parsed[ARG_mask].u_obj != mp_const_none) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("mask not supported yet"));
    }

    py_esp32_image_to_cobj(&image);
    if (!omv_esp32_imlib_mean_filter(&image,
                                     py_esp32_parse_ksize(args[1]),
                                     parsed[ARG_threshold].u_bool,
                                     parsed[ARG_offset].u_int,
                                     parsed[ARG_invert].u_bool)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("mean failed"));
    }
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_mean_obj, 2, py_esp32_image_mean);

static mp_obj_t py_esp32_image_median(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_percentile,
        ARG_threshold,
        ARG_offset,
        ARG_invert,
        ARG_mask,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_percentile, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_threshold, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_offset, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_invert, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_mask, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;
    float percentile = 0.5f;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    if (parsed[ARG_mask].u_obj != mp_const_none) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("mask not supported yet"));
    }
    if (parsed[ARG_percentile].u_obj != mp_const_none) {
        percentile = mp_obj_get_float_to_f(parsed[ARG_percentile].u_obj);
    }
    if ((percentile < 0.0f) || (percentile > 1.0f)) {
        mp_raise_ValueError(MP_ERROR_TEXT("0 <= percentile <= 1"));
    }

    py_esp32_image_to_cobj(&image);
    if (!omv_esp32_imlib_median_filter(&image,
                                       py_esp32_parse_ksize(args[1]),
                                       percentile,
                                       parsed[ARG_threshold].u_bool,
                                       parsed[ARG_offset].u_int,
                                       parsed[ARG_invert].u_bool)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("median failed"));
    }
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_median_obj, 2, py_esp32_image_median);

static mp_obj_t py_esp32_image_gaussian(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_unsharp,
        ARG_threshold,
        ARG_offset,
        ARG_invert,
        ARG_mask,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_unsharp, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_threshold, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_offset, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_invert, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_mask, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    if (parsed[ARG_mask].u_obj != mp_const_none) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("mask not supported yet"));
    }

    py_esp32_image_to_cobj(&image);
    if (!omv_esp32_imlib_gaussian_filter(&image,
                                         py_esp32_parse_ksize(args[1]),
                                         parsed[ARG_unsharp].u_bool,
                                         parsed[ARG_threshold].u_bool,
                                         parsed[ARG_offset].u_int,
                                         parsed[ARG_invert].u_bool)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("gaussian failed"));
    }
    py_esp32_image_mark_dirty();
    py_esp32_image_refresh_preview(&image);
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_gaussian_obj, 2, py_esp32_image_gaussian);

static void py_esp32_blob_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    py_esp32_blob_obj_t *self = MP_OBJ_TO_PTR(self_in);

    (void) kind;
    mp_printf(print,
              "{\"x\":%d, \"y\":%d, \"w\":%d, \"h\":%d, \"pixels\":%d, \"cx\":%d, \"cy\":%d, \"code\":%d, \"count\":%d}",
              mp_obj_get_int(self->x),
              mp_obj_get_int(self->y),
              mp_obj_get_int(self->w),
              mp_obj_get_int(self->h),
              mp_obj_get_int(self->pixels),
              mp_obj_get_int(self->cx),
              mp_obj_get_int(self->cy),
              mp_obj_get_int(self->code),
              mp_obj_get_int(self->count));
}

static mp_obj_t py_esp32_blob_rect(mp_obj_t self_in) {
    py_esp32_blob_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_tuple(4, (mp_obj_t []) {self->x, self->y, self->w, self->h});
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_blob_rect_obj, py_esp32_blob_rect);

static mp_obj_t py_esp32_blob_pixels(mp_obj_t self_in) {
    return ((py_esp32_blob_obj_t *) MP_OBJ_TO_PTR(self_in))->pixels;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_blob_pixels_obj, py_esp32_blob_pixels);

static mp_obj_t py_esp32_blob_cx(mp_obj_t self_in) {
    return ((py_esp32_blob_obj_t *) MP_OBJ_TO_PTR(self_in))->cx;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_blob_cx_obj, py_esp32_blob_cx);

static mp_obj_t py_esp32_blob_cy(mp_obj_t self_in) {
    return ((py_esp32_blob_obj_t *) MP_OBJ_TO_PTR(self_in))->cy;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_blob_cy_obj, py_esp32_blob_cy);

static mp_obj_t py_esp32_blob_code(mp_obj_t self_in) {
    return ((py_esp32_blob_obj_t *) MP_OBJ_TO_PTR(self_in))->code;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_blob_code_obj, py_esp32_blob_code);

static mp_obj_t py_esp32_blob_count(mp_obj_t self_in) {
    return ((py_esp32_blob_obj_t *) MP_OBJ_TO_PTR(self_in))->count;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_blob_count_obj, py_esp32_blob_count);

static const mp_rom_map_elem_t py_esp32_blob_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&py_esp32_blob_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixels), MP_ROM_PTR(&py_esp32_blob_pixels_obj) },
    { MP_ROM_QSTR(MP_QSTR_cx), MP_ROM_PTR(&py_esp32_blob_cx_obj) },
    { MP_ROM_QSTR(MP_QSTR_cy), MP_ROM_PTR(&py_esp32_blob_cy_obj) },
    { MP_ROM_QSTR(MP_QSTR_code), MP_ROM_PTR(&py_esp32_blob_code_obj) },
    { MP_ROM_QSTR(MP_QSTR_count), MP_ROM_PTR(&py_esp32_blob_count_obj) },
};
static MP_DEFINE_CONST_DICT(py_esp32_blob_locals_dict, py_esp32_blob_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    py_esp32_blob_type,
    MP_QSTR_blob,
    MP_TYPE_FLAG_NONE,
    print, py_esp32_blob_print,
    locals_dict, &py_esp32_blob_locals_dict
    );

static mp_obj_t py_esp32_blob_new(const omv_esp32_blob_t *blob) {
    py_esp32_blob_obj_t *o = m_new_obj(py_esp32_blob_obj_t);

    o->base.type = &py_esp32_blob_type;
    o->x = mp_obj_new_int(blob->x);
    o->y = mp_obj_new_int(blob->y);
    o->w = mp_obj_new_int(blob->w);
    o->h = mp_obj_new_int(blob->h);
    o->pixels = mp_obj_new_int(blob->pixels);
    o->cx = mp_obj_new_int((int) lroundf(blob->cx));
    o->cy = mp_obj_new_int((int) lroundf(blob->cy));
    o->code = mp_obj_new_int(blob->code);
    o->count = mp_obj_new_int(blob->count);
    return MP_OBJ_FROM_PTR(o);
}

static void py_esp32_circle_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    py_esp32_circle_obj_t *self = MP_OBJ_TO_PTR(self_in);

    (void) kind;
    mp_printf(print,
              "{\"x\":%d, \"y\":%d, \"r\":%d, \"magnitude\":%d}",
              mp_obj_get_int(self->x),
              mp_obj_get_int(self->y),
              mp_obj_get_int(self->r),
              mp_obj_get_int(self->magnitude));
}

static mp_obj_t py_esp32_circle_circle(mp_obj_t self_in) {
    py_esp32_circle_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_tuple(3, (mp_obj_t []) {self->x, self->y, self->r});
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_circle_circle_obj, py_esp32_circle_circle);

static mp_obj_t py_esp32_circle_magnitude(mp_obj_t self_in) {
    return ((py_esp32_circle_obj_t *) MP_OBJ_TO_PTR(self_in))->magnitude;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_circle_magnitude_obj, py_esp32_circle_magnitude);

static const mp_rom_map_elem_t py_esp32_circle_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&py_esp32_circle_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_magnitude), MP_ROM_PTR(&py_esp32_circle_magnitude_obj) },
};
static MP_DEFINE_CONST_DICT(py_esp32_circle_locals_dict, py_esp32_circle_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    py_esp32_circle_type,
    MP_QSTR_circle,
    MP_TYPE_FLAG_NONE,
    print, py_esp32_circle_print,
    locals_dict, &py_esp32_circle_locals_dict
    );

static mp_obj_t py_esp32_circle_new(const find_circles_list_lnk_data_t *circle) {
    py_esp32_circle_obj_t *o = m_new_obj(py_esp32_circle_obj_t);

    o->base.type = &py_esp32_circle_type;
    o->x = mp_obj_new_int(circle->p.x);
    o->y = mp_obj_new_int(circle->p.y);
    o->r = mp_obj_new_int(circle->r);
    o->magnitude = mp_obj_new_int(circle->magnitude);
    return MP_OBJ_FROM_PTR(o);
}

static void py_esp32_rect_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    py_esp32_rect_obj_t *self = MP_OBJ_TO_PTR(self_in);

    (void) kind;
    mp_print_str(print, "{\"rect\":");
    mp_obj_print_helper(print, self->rect, PRINT_REPR);
    mp_printf(print, ", \"magnitude\":%d}", mp_obj_get_int(self->magnitude));
}

static mp_obj_t py_esp32_rect_corners(mp_obj_t self_in) {
    return ((py_esp32_rect_obj_t *) MP_OBJ_TO_PTR(self_in))->corners;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_rect_corners_obj, py_esp32_rect_corners);

static mp_obj_t py_esp32_rect_rect(mp_obj_t self_in) {
    return ((py_esp32_rect_obj_t *) MP_OBJ_TO_PTR(self_in))->rect;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_rect_rect_obj, py_esp32_rect_rect);

static mp_obj_t py_esp32_rect_magnitude(mp_obj_t self_in) {
    return ((py_esp32_rect_obj_t *) MP_OBJ_TO_PTR(self_in))->magnitude;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_rect_magnitude_obj, py_esp32_rect_magnitude);

static const mp_rom_map_elem_t py_esp32_rect_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_corners), MP_ROM_PTR(&py_esp32_rect_corners_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&py_esp32_rect_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_magnitude), MP_ROM_PTR(&py_esp32_rect_magnitude_obj) },
};
static MP_DEFINE_CONST_DICT(py_esp32_rect_locals_dict, py_esp32_rect_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    py_esp32_rect_type,
    MP_QSTR_rect,
    MP_TYPE_FLAG_NONE,
    print, py_esp32_rect_print,
    locals_dict, &py_esp32_rect_locals_dict
    );

static mp_obj_t py_esp32_rect_new(const find_rects_list_lnk_data_t *rect) {
    py_esp32_rect_obj_t *o = m_new_obj(py_esp32_rect_obj_t);

    o->base.type = &py_esp32_rect_type;
    o->corners = mp_obj_new_tuple(4, (mp_obj_t []) {
        mp_obj_new_tuple(2, (mp_obj_t []) { mp_obj_new_int(rect->corners[0].x), mp_obj_new_int(rect->corners[0].y) }),
        mp_obj_new_tuple(2, (mp_obj_t []) { mp_obj_new_int(rect->corners[1].x), mp_obj_new_int(rect->corners[1].y) }),
        mp_obj_new_tuple(2, (mp_obj_t []) { mp_obj_new_int(rect->corners[2].x), mp_obj_new_int(rect->corners[2].y) }),
        mp_obj_new_tuple(2, (mp_obj_t []) { mp_obj_new_int(rect->corners[3].x), mp_obj_new_int(rect->corners[3].y) }),
    });
    o->rect = mp_obj_new_tuple(4, (mp_obj_t []) {
        mp_obj_new_int(rect->rect.x),
        mp_obj_new_int(rect->rect.y),
        mp_obj_new_int(rect->rect.w),
        mp_obj_new_int(rect->rect.h),
    });
    o->magnitude = mp_obj_new_int(rect->magnitude);
    return MP_OBJ_FROM_PTR(o);
}

#define PY_BARCODE_OBJ_SIZE (8)

static void py_barcode_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    py_barcode_obj_t *self = MP_OBJ_TO_PTR(self_in);

    (void) kind;
    mp_printf(print,
              "{\"x\":%d, \"y\":%d, \"w\":%d, \"h\":%d, \"payload\":\"%s\","
              " \"type\":%d, \"rotation\":%f, \"quality\":%d}",
              mp_obj_get_int(self->x),
              mp_obj_get_int(self->y),
              mp_obj_get_int(self->w),
              mp_obj_get_int(self->h),
              mp_obj_str_get_str(self->payload),
              mp_obj_get_int(self->type),
              mp_obj_get_float_to_d(self->rotation),
              mp_obj_get_int(self->quality));
}

static mp_obj_t py_barcode_subscr(mp_obj_t self_in, mp_obj_t index, mp_obj_t value) {
    if (value == MP_OBJ_SENTINEL) {
        py_barcode_obj_t *self = MP_OBJ_TO_PTR(self_in);

        if (MP_OBJ_IS_TYPE(index, &mp_type_slice)) {
            mp_bound_slice_t slice;

            if (!mp_seq_get_fast_slice_indexes(PY_BARCODE_OBJ_SIZE, index, &slice)) {
                mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("only step=1 slices are supported"));
            }

            mp_obj_tuple_t *result = mp_obj_new_tuple(slice.stop - slice.start, NULL);
            mp_seq_copy(result->items, &(self->x) + slice.start, result->len, mp_obj_t);
            return MP_OBJ_FROM_PTR(result);
        }

        switch (mp_get_index(self->base.type, PY_BARCODE_OBJ_SIZE, index, false)) {
            case 0: return self->x;
            case 1: return self->y;
            case 2: return self->w;
            case 3: return self->h;
            case 4: return self->payload;
            case 5: return self->type;
            case 6: return self->rotation;
            case 7: return self->quality;
        }
    }

    return MP_OBJ_NULL;
}

static mp_obj_t py_barcode_corners(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->corners;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_corners_obj, py_barcode_corners);

static mp_obj_t py_barcode_rect(mp_obj_t self_in) {
    py_barcode_obj_t *self = MP_OBJ_TO_PTR(self_in);

    return mp_obj_new_tuple(4, (mp_obj_t []) {self->x, self->y, self->w, self->h});
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_rect_obj, py_barcode_rect);

static mp_obj_t py_barcode_x(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->x;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_x_obj, py_barcode_x);

static mp_obj_t py_barcode_y(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->y;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_y_obj, py_barcode_y);

static mp_obj_t py_barcode_w(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->w;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_w_obj, py_barcode_w);

static mp_obj_t py_barcode_h(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->h;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_h_obj, py_barcode_h);

static mp_obj_t py_barcode_payload(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->payload;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_payload_obj, py_barcode_payload);

static mp_obj_t py_barcode_type_fun(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->type;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_type_fun_obj, py_barcode_type_fun);

static mp_obj_t py_barcode_rotation(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->rotation;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_rotation_obj, py_barcode_rotation);

static mp_obj_t py_barcode_quality(mp_obj_t self_in) {
    return ((py_barcode_obj_t *) MP_OBJ_TO_PTR(self_in))->quality;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_barcode_quality_obj, py_barcode_quality);

static const mp_rom_map_elem_t py_barcode_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_corners), MP_ROM_PTR(&py_barcode_corners_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&py_barcode_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_x), MP_ROM_PTR(&py_barcode_x_obj) },
    { MP_ROM_QSTR(MP_QSTR_y), MP_ROM_PTR(&py_barcode_y_obj) },
    { MP_ROM_QSTR(MP_QSTR_h), MP_ROM_PTR(&py_barcode_h_obj) },
    { MP_ROM_QSTR(MP_QSTR_payload), MP_ROM_PTR(&py_barcode_payload_obj) },
    { MP_ROM_QSTR(MP_QSTR_type), MP_ROM_PTR(&py_barcode_type_fun_obj) },
    { MP_ROM_QSTR(MP_QSTR_rotation), MP_ROM_PTR(&py_barcode_rotation_obj) },
    { MP_ROM_QSTR(MP_QSTR_quality), MP_ROM_PTR(&py_barcode_quality_obj) },
};
static MP_DEFINE_CONST_DICT(py_barcode_locals_dict, py_barcode_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    py_barcode_type,
    MP_QSTR_barcode,
    MP_TYPE_FLAG_NONE,
    print, py_barcode_print,
    subscr, py_barcode_subscr,
    attr, py_barcode_attr,
    locals_dict, &py_barcode_locals_dict
    );

static void py_barcode_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (dest[0] != MP_OBJ_NULL) {
        return;
    }

    if (strcmp(qstr_str(attr), "w") == 0) {
        dest[0] = MP_OBJ_FROM_PTR(&py_barcode_w_obj);
        dest[1] = self_in;
        return;
    }

    // Defer everything else to locals_dict so rect()/payload()/type() work.
    dest[1] = MP_OBJ_SENTINEL;
}

static mp_obj_t py_esp32_image_find_barcodes(mp_obj_t self_in) {
    omv_barcode_list_t barcodes;
    mp_obj_list_t *objects_list;

    (void) self_in;
    if (!omv_bar_find_barcodes(&barcodes)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("barcode scan failed"));
    }

    objects_list = MP_OBJ_TO_PTR(mp_obj_new_list(barcodes.count, NULL));
    for (size_t i = 0; i < barcodes.count; i++) {
        const omv_barcode_t *barcode = &barcodes.items[i];
        py_barcode_obj_t *o = m_new_obj(py_barcode_obj_t);

        o->base.type = &py_barcode_type;
        o->corners = mp_obj_new_tuple(4, (mp_obj_t []) {
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(barcode->corners[0].x), mp_obj_new_int(barcode->corners[0].y)}),
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(barcode->corners[1].x), mp_obj_new_int(barcode->corners[1].y)}),
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(barcode->corners[2].x), mp_obj_new_int(barcode->corners[2].y)}),
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(barcode->corners[3].x), mp_obj_new_int(barcode->corners[3].y)}),
        });
        o->x = mp_obj_new_int(barcode->rect.x);
        o->y = mp_obj_new_int(barcode->rect.y);
        o->w = mp_obj_new_int(barcode->rect.w);
        o->h = mp_obj_new_int(barcode->rect.h);
        o->payload = mp_obj_new_str(barcode->payload, barcode->payload_len);
        o->type = mp_obj_new_int(barcode->type);
        o->rotation = mp_obj_new_float(((mp_float_t) barcode->rotation) * ((mp_float_t) M_PI / 180.0f));
        o->quality = mp_obj_new_int(barcode->quality);
        objects_list->items[i] = MP_OBJ_FROM_PTR(o);
    }

    omv_bar_free_barcodes(&barcodes);
    return MP_OBJ_FROM_PTR(objects_list);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_find_barcodes_obj, py_esp32_image_find_barcodes);

#define PY_QRCODE_OBJ_SIZE (10)

static void py_qrcode_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    py_qrcode_obj_t *self = MP_OBJ_TO_PTR(self_in);

    (void) kind;
    mp_printf(print,
              "{\"x\":%d, \"y\":%d, \"w\":%d, \"h\":%d, \"payload\":\"%s\","
              " \"version\":%d, \"ecc_level\":%d, \"mask\":%d, \"data_type\":%d, \"eci\":%d}",
              mp_obj_get_int(self->x),
              mp_obj_get_int(self->y),
              mp_obj_get_int(self->w),
              mp_obj_get_int(self->h),
              mp_obj_str_get_str(self->payload),
              mp_obj_get_int(self->version),
              mp_obj_get_int(self->ecc_level),
              mp_obj_get_int(self->mask),
              mp_obj_get_int(self->data_type),
              mp_obj_get_int(self->eci));
}

static mp_obj_t py_qrcode_subscr(mp_obj_t self_in, mp_obj_t index, mp_obj_t value) {
    if (value == MP_OBJ_SENTINEL) {
        py_qrcode_obj_t *self = MP_OBJ_TO_PTR(self_in);

        if (MP_OBJ_IS_TYPE(index, &mp_type_slice)) {
            mp_bound_slice_t slice;

            if (!mp_seq_get_fast_slice_indexes(PY_QRCODE_OBJ_SIZE, index, &slice)) {
                mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("only step=1 slices are supported"));
            }

            mp_obj_tuple_t *result = mp_obj_new_tuple(slice.stop - slice.start, NULL);
            mp_seq_copy(result->items, &(self->x) + slice.start, result->len, mp_obj_t);
            return MP_OBJ_FROM_PTR(result);
        }

        switch (mp_get_index(self->base.type, PY_QRCODE_OBJ_SIZE, index, false)) {
            case 0: return self->x;
            case 1: return self->y;
            case 2: return self->w;
            case 3: return self->h;
            case 4: return self->payload;
            case 5: return self->version;
            case 6: return self->ecc_level;
            case 7: return self->mask;
            case 8: return self->data_type;
            case 9: return self->eci;
        }
    }

    return MP_OBJ_NULL;
}

static mp_obj_t py_qrcode_corners(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->corners;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_corners_obj, py_qrcode_corners);

static mp_obj_t py_qrcode_rect(mp_obj_t self_in) {
    py_qrcode_obj_t *self = MP_OBJ_TO_PTR(self_in);

    return mp_obj_new_tuple(4, (mp_obj_t []) {self->x, self->y, self->w, self->h});
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_rect_obj, py_qrcode_rect);

static mp_obj_t py_qrcode_x(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->x;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_x_obj, py_qrcode_x);

static mp_obj_t py_qrcode_y(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->y;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_y_obj, py_qrcode_y);

static mp_obj_t py_qrcode_w(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->w;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_w_obj, py_qrcode_w);

static mp_obj_t py_qrcode_h(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->h;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_h_obj, py_qrcode_h);

static mp_obj_t py_qrcode_payload(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->payload;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_payload_obj, py_qrcode_payload);

static mp_obj_t py_qrcode_version(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->version;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_version_obj, py_qrcode_version);

static mp_obj_t py_qrcode_ecc_level(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->ecc_level;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_ecc_level_obj, py_qrcode_ecc_level);

static mp_obj_t py_qrcode_mask(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->mask;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_mask_obj, py_qrcode_mask);

static mp_obj_t py_qrcode_data_type(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->data_type;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_data_type_obj, py_qrcode_data_type);

static mp_obj_t py_qrcode_eci(mp_obj_t self_in) {
    return ((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->eci;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_eci_obj, py_qrcode_eci);

static mp_obj_t py_qrcode_is_numeric(mp_obj_t self_in) {
    return mp_obj_new_bool(mp_obj_get_int(((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->data_type) == 1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_is_numeric_obj, py_qrcode_is_numeric);

static mp_obj_t py_qrcode_is_alphanumeric(mp_obj_t self_in) {
    return mp_obj_new_bool(mp_obj_get_int(((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->data_type) == 2);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_is_alphanumeric_obj, py_qrcode_is_alphanumeric);

static mp_obj_t py_qrcode_is_binary(mp_obj_t self_in) {
    return mp_obj_new_bool(mp_obj_get_int(((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->data_type) == 4);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_is_binary_obj, py_qrcode_is_binary);

static mp_obj_t py_qrcode_is_kanji(mp_obj_t self_in) {
    return mp_obj_new_bool(mp_obj_get_int(((py_qrcode_obj_t *) MP_OBJ_TO_PTR(self_in))->data_type) == 8);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_qrcode_is_kanji_obj, py_qrcode_is_kanji);

static const mp_rom_map_elem_t py_qrcode_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_corners), MP_ROM_PTR(&py_qrcode_corners_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&py_qrcode_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_x), MP_ROM_PTR(&py_qrcode_x_obj) },
    { MP_ROM_QSTR(MP_QSTR_y), MP_ROM_PTR(&py_qrcode_y_obj) },
    { MP_ROM_QSTR(MP_QSTR_h), MP_ROM_PTR(&py_qrcode_h_obj) },
    { MP_ROM_QSTR(MP_QSTR_payload), MP_ROM_PTR(&py_qrcode_payload_obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&py_qrcode_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_ecc_level), MP_ROM_PTR(&py_qrcode_ecc_level_obj) },
    { MP_ROM_QSTR(MP_QSTR_mask), MP_ROM_PTR(&py_qrcode_mask_obj) },
    { MP_ROM_QSTR(MP_QSTR_data_type), MP_ROM_PTR(&py_qrcode_data_type_obj) },
    { MP_ROM_QSTR(MP_QSTR_eci), MP_ROM_PTR(&py_qrcode_eci_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_numeric), MP_ROM_PTR(&py_qrcode_is_numeric_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_alphanumeric), MP_ROM_PTR(&py_qrcode_is_alphanumeric_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_binary), MP_ROM_PTR(&py_qrcode_is_binary_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_kanji), MP_ROM_PTR(&py_qrcode_is_kanji_obj) },
};
static MP_DEFINE_CONST_DICT(py_qrcode_locals_dict, py_qrcode_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    py_qrcode_type,
    MP_QSTR_qrcode,
    MP_TYPE_FLAG_NONE,
    print, py_qrcode_print,
    subscr, py_qrcode_subscr,
    attr, py_qrcode_attr,
    locals_dict, &py_qrcode_locals_dict
    );

static void py_qrcode_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (dest[0] != MP_OBJ_NULL) {
        return;
    }

    if (strcmp(qstr_str(attr), "w") == 0) {
        dest[0] = MP_OBJ_FROM_PTR(&py_qrcode_w_obj);
        dest[1] = self_in;
        return;
    }

    dest[1] = MP_OBJ_SENTINEL;
}

static mp_obj_t py_esp32_image_find_qrcodes(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_roi,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_roi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;
    rectangle_t roi;
    rectangle_t *roi_ptr = NULL;
    omv_qrcode_list_t qrcodes;
    mp_obj_list_t *objects_list;

    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_image_to_cobj(&image);

    if (parsed[ARG_roi].u_obj != MP_OBJ_NULL) {
        py_esp32_fill_roi(&image, parsed[ARG_roi].u_obj, &roi);
        roi_ptr = &roi;
    }

    if (!omv_qr_find_qrcodes(&qrcodes, roi_ptr)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("qrcode scan failed"));
    }

    objects_list = MP_OBJ_TO_PTR(mp_obj_new_list(qrcodes.count, NULL));
    for (size_t i = 0; i < qrcodes.count; i++) {
        const omv_qrcode_t *qrcode = &qrcodes.items[i];
        py_qrcode_obj_t *o = m_new_obj(py_qrcode_obj_t);

        o->base.type = &py_qrcode_type;
        o->corners = mp_obj_new_tuple(4, (mp_obj_t []) {
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(qrcode->corners[0].x), mp_obj_new_int(qrcode->corners[0].y)}),
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(qrcode->corners[1].x), mp_obj_new_int(qrcode->corners[1].y)}),
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(qrcode->corners[2].x), mp_obj_new_int(qrcode->corners[2].y)}),
            mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_int(qrcode->corners[3].x), mp_obj_new_int(qrcode->corners[3].y)}),
        });
        o->x = mp_obj_new_int(qrcode->rect.x);
        o->y = mp_obj_new_int(qrcode->rect.y);
        o->w = mp_obj_new_int(qrcode->rect.w);
        o->h = mp_obj_new_int(qrcode->rect.h);
        o->payload = mp_obj_new_str(qrcode->payload, qrcode->payload_len);
        o->version = mp_obj_new_int(qrcode->version);
        o->ecc_level = mp_obj_new_int(qrcode->ecc_level);
        o->mask = mp_obj_new_int(qrcode->mask);
        o->data_type = mp_obj_new_int(qrcode->data_type);
        o->eci = mp_obj_new_int_from_uint(qrcode->eci);
        objects_list->items[i] = MP_OBJ_FROM_PTR(o);
    }

    omv_qr_free_qrcodes(&qrcodes);
    return MP_OBJ_FROM_PTR(objects_list);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_find_qrcodes_obj, 1, py_esp32_image_find_qrcodes);

static mp_obj_t py_esp32_image_find_blobs(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_roi,
        ARG_invert,
        ARG_pixels_threshold,
        ARG_area_threshold,
        ARG_merge,
        ARG_margin,
        ARG_x_stride,
        ARG_y_stride,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_roi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_invert, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_pixels_threshold, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_area_threshold, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_merge, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_margin, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_x_stride, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_y_stride, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;
    rectangle_t roi;
    omv_esp32_threshold_t *thresholds = NULL;
    omv_esp32_blob_t *blobs = NULL;
    size_t thresholds_len;
    size_t blobs_len = 0;

    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    if (parsed[ARG_merge].u_bool) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("merge not supported yet"));
    }
    if (parsed[ARG_margin].u_int != 0) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("margin not supported yet"));
    }
    if ((parsed[ARG_x_stride].u_int <= 0) || (parsed[ARG_y_stride].u_int <= 0)) {
        mp_raise_ValueError(MP_ERROR_TEXT("x_stride/y_stride must be > 0"));
    }

    py_esp32_image_to_cobj(&image);
    py_esp32_fill_roi(&image, parsed[ARG_roi].u_obj, &roi);
    thresholds_len = py_esp32_parse_blob_thresholds(args[1], &thresholds);

    bool ok = omv_esp32_find_blobs(&image,
                                   &roi,
                                   thresholds,
                                   thresholds_len,
                                   parsed[ARG_invert].u_bool,
                                   parsed[ARG_x_stride].u_int,
                                   parsed[ARG_y_stride].u_int,
                                   parsed[ARG_area_threshold].u_int,
                                   parsed[ARG_pixels_threshold].u_int,
                                   &blobs,
                                   &blobs_len);
    py_esp32_free_blob_thresholds(thresholds, thresholds_len);

    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("find_blobs failed"));
    }

    mp_obj_list_t *objects_list = MP_OBJ_TO_PTR(mp_obj_new_list(blobs_len, NULL));
    for (size_t i = 0; i < blobs_len; i++) {
        objects_list->items[i] = py_esp32_blob_new(&blobs[i]);
    }
    omv_esp32_free_blobs(blobs);
    return MP_OBJ_FROM_PTR(objects_list);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_find_blobs_obj, 2, py_esp32_image_find_blobs);

static mp_obj_t py_esp32_image_find_circles(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_roi,
        ARG_x_stride,
        ARG_y_stride,
        ARG_threshold,
        ARG_x_margin,
        ARG_y_margin,
        ARG_r_margin,
        ARG_r_min,
        ARG_r_max,
        ARG_r_step,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_roi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_x_stride, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2} },
        { MP_QSTR_y_stride, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_threshold, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2000} },
        { MP_QSTR_x_margin, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_y_margin, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_r_margin, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_r_min, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2} },
        { MP_QSTR_r_max, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_r_step, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 2} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;
    rectangle_t roi;
    find_circles_list_lnk_data_t *circles = NULL;
    size_t circles_len = 0;

    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_image_to_cobj(&image);
    py_esp32_fill_roi(&image, parsed[ARG_roi].u_obj, &roi);

    unsigned int r_max = parsed[ARG_r_max].u_int < 0 ? (unsigned int) IM_MIN(roi.w / 2, roi.h / 2)
                                                     : (unsigned int) parsed[ARG_r_max].u_int;
    bool ok = omv_esp32_find_circles(&image,
                                     &roi,
                                     parsed[ARG_x_stride].u_int,
                                     parsed[ARG_y_stride].u_int,
                                     parsed[ARG_threshold].u_int,
                                     parsed[ARG_x_margin].u_int,
                                     parsed[ARG_y_margin].u_int,
                                     parsed[ARG_r_margin].u_int,
                                     IM_MAX(parsed[ARG_r_min].u_int, 2),
                                     r_max,
                                     IM_MAX(parsed[ARG_r_step].u_int, 1),
                                     &circles,
                                     &circles_len);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("find_circles failed"));
    }

    mp_obj_list_t *objects_list = MP_OBJ_TO_PTR(mp_obj_new_list(circles_len, NULL));
    for (size_t i = 0; i < circles_len; i++) {
        objects_list->items[i] = py_esp32_circle_new(&circles[i]);
    }
    omv_esp32_free_circles(circles);
    return MP_OBJ_FROM_PTR(objects_list);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_find_circles_obj, 1, py_esp32_image_find_circles);

static mp_obj_t py_esp32_image_find_rects(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_roi,
        ARG_threshold,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_roi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_threshold, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    image_t image;
    rectangle_t roi;
    find_rects_list_lnk_data_t *rects = NULL;
    size_t rects_len = 0;

    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    py_esp32_image_to_cobj(&image);
    py_esp32_fill_roi(&image, parsed[ARG_roi].u_obj, &roi);

    bool ok = omv_esp32_find_rects(&image, &roi, parsed[ARG_threshold].u_int, &rects, &rects_len);
    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("find_rects failed"));
    }

    mp_obj_list_t *objects_list = MP_OBJ_TO_PTR(mp_obj_new_list(rects_len, NULL));
    for (size_t i = 0; i < rects_len; i++) {
        objects_list->items[i] = py_esp32_rect_new(&rects[i]);
    }
    omv_esp32_free_rects(rects);
    return MP_OBJ_FROM_PTR(objects_list);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_find_rects_obj, 1, py_esp32_image_find_rects);

static void py_esp32_image_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void) self_in;
    (void) kind;

    framebuffer_t *fb = py_esp32_image_fb();
    mp_printf(print,
              "{\"w\":%d, \"h\":%d, \"type\":%u, \"size\":%u}",
              fb->w,
              fb->h,
              (unsigned int) fb->pixfmt,
              (unsigned int) fb->size);
}

static const mp_rom_map_elem_t py_esp32_image_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&py_esp32_image_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&py_esp32_image_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_format), MP_ROM_PTR(&py_esp32_image_format_obj) },
    { MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&py_esp32_image_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&py_esp32_image_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_line), MP_ROM_PTR(&py_esp32_image_draw_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_rectangle), MP_ROM_PTR(&py_esp32_image_draw_rectangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_circle), MP_ROM_PTR(&py_esp32_image_draw_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_string), MP_ROM_PTR(&py_esp32_image_draw_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_cross), MP_ROM_PTR(&py_esp32_image_draw_cross_obj) },
    { MP_ROM_QSTR(MP_QSTR_binary), MP_ROM_PTR(&py_esp32_image_binary_obj) },
    { MP_ROM_QSTR(MP_QSTR_invert), MP_ROM_PTR(&py_esp32_image_invert_obj) },
    { MP_ROM_QSTR(MP_QSTR_negate), MP_ROM_PTR(&py_esp32_image_invert_obj) },
    { MP_ROM_QSTR(MP_QSTR_mean), MP_ROM_PTR(&py_esp32_image_mean_obj) },
    { MP_ROM_QSTR(MP_QSTR_median), MP_ROM_PTR(&py_esp32_image_median_obj) },
    { MP_ROM_QSTR(MP_QSTR_gaussian), MP_ROM_PTR(&py_esp32_image_gaussian_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_barcodes), MP_ROM_PTR(&py_esp32_image_find_barcodes_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_qrcodes), MP_ROM_PTR(&py_esp32_image_find_qrcodes_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_blobs), MP_ROM_PTR(&py_esp32_image_find_blobs_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_circles), MP_ROM_PTR(&py_esp32_image_find_circles_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_edges), MP_ROM_PTR(&py_esp32_image_find_edges_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_rects), MP_ROM_PTR(&py_esp32_image_find_rects_obj) },
};
static MP_DEFINE_CONST_DICT(py_esp32_image_locals_dict, py_esp32_image_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    py_esp32_image_type,
    MP_QSTR_Image,
    MP_TYPE_FLAG_NONE,
    print, py_esp32_image_print,
    locals_dict, &py_esp32_image_locals_dict
    );

void py_esp32_image_init0(void) {
    py_esp32_image_obj.base.type = &py_esp32_image_type;
}

mp_obj_t py_esp32_image_from_mainfb(void) {
    if (py_esp32_image_obj.base.type == NULL) {
        py_esp32_image_init0();
    }

    return MP_OBJ_FROM_PTR(&py_esp32_image_obj);
}

static const mp_rom_map_elem_t py_esp32_image_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_image) },
    { MP_ROM_QSTR(MP_QSTR_Image), MP_ROM_PTR(&py_esp32_image_type) },
    { MP_ROM_QSTR(MP_QSTR_EAN2), MP_ROM_INT(BARCODE_EAN2) },
    { MP_ROM_QSTR(MP_QSTR_EAN5), MP_ROM_INT(BARCODE_EAN5) },
    { MP_ROM_QSTR(MP_QSTR_EAN8), MP_ROM_INT(BARCODE_EAN8) },
    { MP_ROM_QSTR(MP_QSTR_UPCE), MP_ROM_INT(BARCODE_UPCE) },
    { MP_ROM_QSTR(MP_QSTR_ISBN10), MP_ROM_INT(BARCODE_ISBN10) },
    { MP_ROM_QSTR(MP_QSTR_UPCA), MP_ROM_INT(BARCODE_UPCA) },
    { MP_ROM_QSTR(MP_QSTR_EAN13), MP_ROM_INT(BARCODE_EAN13) },
    { MP_ROM_QSTR(MP_QSTR_ISBN13), MP_ROM_INT(BARCODE_ISBN13) },
    { MP_ROM_QSTR(MP_QSTR_I25), MP_ROM_INT(BARCODE_I25) },
    { MP_ROM_QSTR(MP_QSTR_DATABAR), MP_ROM_INT(BARCODE_DATABAR) },
    { MP_ROM_QSTR(MP_QSTR_DATABAR_EXP), MP_ROM_INT(BARCODE_DATABAR_EXP) },
    { MP_ROM_QSTR(MP_QSTR_CODABAR), MP_ROM_INT(BARCODE_CODABAR) },
    { MP_ROM_QSTR(MP_QSTR_CODE39), MP_ROM_INT(BARCODE_CODE39) },
    { MP_ROM_QSTR(MP_QSTR_PDF417), MP_ROM_INT(BARCODE_PDF417) },
    { MP_ROM_QSTR(MP_QSTR_CODE93), MP_ROM_INT(BARCODE_CODE93) },
    { MP_ROM_QSTR(MP_QSTR_CODE128), MP_ROM_INT(BARCODE_CODE128) },
    { MP_ROM_QSTR(MP_QSTR_EDGE_CANNY), MP_ROM_INT(EDGE_CANNY) },
    { MP_ROM_QSTR(MP_QSTR_EDGE_SIMPLE), MP_ROM_INT(EDGE_SIMPLE) },
};
static MP_DEFINE_CONST_DICT(py_esp32_image_module_globals, py_esp32_image_module_globals_table);

const mp_obj_module_t image_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *) &py_esp32_image_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_image, image_module);
