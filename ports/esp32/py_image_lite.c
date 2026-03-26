/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal Image object for the ESP32 OpenMV bring-up path.
 */

#include <stdbool.h>

#include "py/objtuple.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include "framebuffer.h"
#include "imlib.h"

#include "omv_imlib_blob_min.h"
#include "omv_imlib_edge_min.h"
#include "omv_imlib_filter_min.h"
#include "omv_imlib_gray_min.h"
#include "omv_imlib_shape_min.h"
#include "py_image_lite.h"

typedef struct _py_esp32_image_obj_t {
    mp_obj_base_t base;
} py_esp32_image_obj_t;

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

static framebuffer_t *py_esp32_image_fb(void) {
    return framebuffer_get(FB_MAINFB_ID);
}

static image_t py_esp32_image_make_image(void) {
    framebuffer_t *fb = py_esp32_image_fb();
    image_t img = {
        .w = fb->w,
        .h = fb->h,
        .pixfmt = fb->pixfmt,
        .size = fb->size,
        .pixels = (uint8_t *) fb->raw_base,
    };
    return img;
}

static int py_esp32_image_parse_color(mp_obj_t color_obj, int default_color) {
    if (color_obj == MP_OBJ_NULL) {
        return default_color;
    }

    if (mp_obj_is_int(color_obj)) {
        return mp_obj_get_int(color_obj);
    }

    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(color_obj, &len, &items);
    if (len != 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("color must be int or (r,g,b)"));
    }

    return COLOR_R8_G8_B8_TO_RGB565(mp_obj_get_int(items[0]),
                                    mp_obj_get_int(items[1]),
                                    mp_obj_get_int(items[2]));
}

static int py_esp32_image_parse_ksize(mp_obj_t ksize_obj) {
    int ksize = mp_obj_get_int(ksize_obj);
    if (ksize < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("ksize must be >= 0"));
    }
    return ksize;
}

static void py_esp32_image_check_xy(image_t *img, int x, int y) {
    if ((x < 0) || (y < 0) || (x >= img->w) || (y >= img->h)) {
        mp_raise_ValueError(MP_ERROR_TEXT("pixel out of bounds"));
    }
}

static void py_esp32_image_parse_binary_threshold(mp_obj_t thresholds_obj, int *lo, int *hi) {
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(thresholds_obj, &len, &items);

    if (len == 2 && mp_obj_is_int(items[0]) && mp_obj_is_int(items[1])) {
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

static void py_esp32_image_parse_roi(image_t *img, mp_obj_t roi_obj, rectangle_t *roi) {
    if ((roi_obj == MP_OBJ_NULL) || (roi_obj == mp_const_none)) {
        roi->x = 0;
        roi->y = 0;
        roi->w = img->w;
        roi->h = img->h;
        return;
    }

    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(roi_obj, &len, &items);
    if (len != 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("roi must be (x,y,w,h)"));
    }

    roi->x = mp_obj_get_int(items[0]);
    roi->y = mp_obj_get_int(items[1]);
    roi->w = mp_obj_get_int(items[2]);
    roi->h = mp_obj_get_int(items[3]);

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

static size_t py_esp32_image_parse_blob_thresholds(mp_obj_t thresholds_obj, omv_esp32_threshold_t **out) {
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(thresholds_obj, &len, &items);

    bool single_tuple = false;
    if (len == 2 && mp_obj_is_int(items[0]) && mp_obj_is_int(items[1])) {
        single_tuple = true;
    } else if (len == 6) {
        single_tuple = true;
    }

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

static void py_esp32_image_free_blob_thresholds(omv_esp32_threshold_t *thresholds, size_t len) {
    m_del(omv_esp32_threshold_t, thresholds, len);
}

static void py_esp32_blob_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void) kind;
    py_esp32_blob_obj_t *self = MP_OBJ_TO_PTR(self_in);
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
    mp_obj_t tuple[4] = {self->x, self->y, self->w, self->h};
    return mp_obj_new_tuple(4, tuple);
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

MP_DEFINE_CONST_OBJ_TYPE(
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
    o->cx = mp_obj_new_int((int) fast_roundf(blob->cx));
    o->cy = mp_obj_new_int((int) fast_roundf(blob->cy));
    o->code = mp_obj_new_int(blob->code);
    o->count = mp_obj_new_int(blob->count);
    return MP_OBJ_FROM_PTR(o);
}

static void py_esp32_circle_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void) kind;
    py_esp32_circle_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print,
              "{\"x\":%d, \"y\":%d, \"r\":%d, \"magnitude\":%d}",
              mp_obj_get_int(self->x),
              mp_obj_get_int(self->y),
              mp_obj_get_int(self->r),
              mp_obj_get_int(self->magnitude));
}

static mp_obj_t py_esp32_circle_circle(mp_obj_t self_in) {
    py_esp32_circle_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t tuple[3] = {self->x, self->y, self->r};
    return mp_obj_new_tuple(3, tuple);
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

MP_DEFINE_CONST_OBJ_TYPE(
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
    (void) kind;
    py_esp32_rect_obj_t *self = MP_OBJ_TO_PTR(self_in);
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

MP_DEFINE_CONST_OBJ_TYPE(
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
    return mp_obj_new_int(py_esp32_image_fb()->pixfmt);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_format_obj, py_esp32_image_format);

static mp_obj_t py_esp32_image_size(mp_obj_t self_in) {
    (void) self_in;
    return mp_obj_new_int_from_uint(py_esp32_image_fb()->size);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_size_obj, py_esp32_image_size);

static mp_obj_t py_esp32_image_get_pixel(mp_obj_t self_in, mp_obj_t x_in, mp_obj_t y_in) {
    (void) self_in;
    image_t img = py_esp32_image_make_image();
    int x = mp_obj_get_int(x_in);
    int y = mp_obj_get_int(y_in);

    py_esp32_image_check_xy(&img, x, y);
    uint16_t pixel = imlib_get_pixel(&img, x, y);

    mp_obj_t tuple[3] = {
        mp_obj_new_int(COLOR_RGB565_TO_R8(pixel)),
        mp_obj_new_int(COLOR_RGB565_TO_G8(pixel)),
        mp_obj_new_int(COLOR_RGB565_TO_B8(pixel)),
    };
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_esp32_image_get_pixel_obj, py_esp32_image_get_pixel);

static mp_obj_t py_esp32_image_set_pixel(size_t n_args, const mp_obj_t *args) {
    (void) n_args;
    image_t img = py_esp32_image_make_image();
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int color = py_esp32_image_parse_color(args[3], -1);

    py_esp32_image_check_xy(&img, x, y);
    imlib_set_pixel(&img, x, y, color);
    omv_esp32_imlib_gray_mark_dirty();
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_esp32_image_set_pixel_obj, 4, 4, py_esp32_image_set_pixel);

static mp_obj_t py_esp32_image_draw_line(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_thickness,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    size_t len;
    mp_obj_t *vec;
    mp_obj_get_array(args[1], &len, &vec);
    if (len != 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("line must be (x0,y0,x1,y1)"));
    }

    image_t img = py_esp32_image_make_image();
    int color = py_esp32_image_parse_color(parsed[ARG_color].u_obj, -1);
    imlib_draw_line(&img,
                    mp_obj_get_int(vec[0]),
                    mp_obj_get_int(vec[1]),
                    mp_obj_get_int(vec[2]),
                    mp_obj_get_int(vec[3]),
                    color,
                    parsed[ARG_thickness].u_int);
    omv_esp32_imlib_gray_mark_dirty();
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
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_fill, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    size_t len;
    mp_obj_t *vec;
    mp_obj_get_array(args[1], &len, &vec);
    if (len != 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("rectangle must be (x,y,w,h)"));
    }

    image_t img = py_esp32_image_make_image();
    int color = py_esp32_image_parse_color(parsed[ARG_color].u_obj, -1);
    imlib_draw_rectangle(&img,
                         mp_obj_get_int(vec[0]),
                         mp_obj_get_int(vec[1]),
                         mp_obj_get_int(vec[2]),
                         mp_obj_get_int(vec[3]),
                         color,
                         parsed[ARG_thickness].u_int,
                         parsed[ARG_fill].u_bool);
    omv_esp32_imlib_gray_mark_dirty();
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_rectangle_obj, 2, py_esp32_image_draw_rectangle);

static mp_obj_t py_esp32_image_draw_cross(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_size,
        ARG_thickness,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_size, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 5} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 3, args + 3, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    image_t img = py_esp32_image_make_image();
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int color = py_esp32_image_parse_color(parsed[ARG_color].u_obj, -1);
    int size = parsed[ARG_size].u_int;
    int thickness = parsed[ARG_thickness].u_int;

    imlib_draw_line(&img, x - size, y, x + size, y, color, thickness);
    imlib_draw_line(&img, x, y - size, x, y + size, color, thickness);
    omv_esp32_imlib_gray_mark_dirty();
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_cross_obj, 3, py_esp32_image_draw_cross);

static mp_obj_t py_esp32_image_draw_circle(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum {
        ARG_color,
        ARG_thickness,
        ARG_fill,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_color, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_thickness, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_fill, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 4, args + 4, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    image_t img = py_esp32_image_make_image();
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int radius = mp_obj_get_int(args[3]);
    int color = py_esp32_image_parse_color(parsed[ARG_color].u_obj, -1);

    imlib_draw_circle(&img,
                      x,
                      y,
                      radius,
                      color,
                      parsed[ARG_thickness].u_int,
                      parsed[ARG_fill].u_bool);
    omv_esp32_imlib_gray_mark_dirty();
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_draw_circle_obj, 4, py_esp32_image_draw_circle);

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
    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    image_t img = py_esp32_image_make_image();
    if (img.pixfmt != PIXFORMAT_RGB565) {
        mp_raise_ValueError(MP_ERROR_TEXT("binary only supports RGB565"));
    }

    int lo, hi;
    py_esp32_image_parse_binary_threshold(parsed[ARG_thresholds].u_obj, &lo, &hi);

    uint16_t *pixels = (uint16_t *) img.pixels;
    size_t count = (size_t) img.w * img.h;
    bool invert = parsed[ARG_invert].u_bool;

    for (size_t i = 0; i < count; i++) {
        uint16_t pixel = pixels[i];
        int y = COLOR_RGB565_TO_Y(pixel);
        bool in_range = (y >= lo) && (y <= hi);
        if (invert) {
            in_range = !in_range;
        }
        pixels[i] = in_range ? COLOR_R8_G8_B8_TO_RGB565(255, 255, 255)
                             : COLOR_R8_G8_B8_TO_RGB565(0, 0, 0);
    }

    omv_esp32_imlib_gray_mark_dirty();
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_binary_obj, 2, py_esp32_image_binary);

static mp_obj_t py_esp32_image_invert(mp_obj_t self_in) {
    image_t img = py_esp32_image_make_image();
    imlib_invert(&img);
    omv_esp32_imlib_gray_mark_dirty();
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
        { MP_QSTR_roi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    image_t img = py_esp32_image_make_image();
    rectangle_t roi;
    py_esp32_image_parse_roi(&img, parsed[ARG_roi].u_obj, &roi);

    int low_thresh = 100;
    int high_thresh = 200;
    if (parsed[ARG_threshold].u_obj != mp_const_none) {
        size_t len;
        mp_obj_t *items;
        mp_obj_get_array(parsed[ARG_threshold].u_obj, &len, &items);
        if (len != 2) {
            mp_raise_ValueError(MP_ERROR_TEXT("threshold must be (low,high)"));
        }
        low_thresh = mp_obj_get_int(items[0]);
        high_thresh = mp_obj_get_int(items[1]);
    }

    py_esp32_threshold_sort_pair(&low_thresh, &high_thresh);

    bool ok = false;
    switch (mp_obj_get_int(args[1])) {
        case EDGE_SIMPLE:
            ok = omv_esp32_imlib_edge_simple(&img, &roi, low_thresh, high_thresh);
            break;
        case EDGE_CANNY:
            ok = omv_esp32_imlib_edge_canny(&img, &roi, low_thresh, high_thresh);
            break;
        default:
            mp_raise_ValueError(MP_ERROR_TEXT("unsupported edge detector"));
    }

    if (!ok) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("find_edges failed"));
    }

    omv_esp32_imlib_gray_mark_dirty();
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
    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    if (parsed[ARG_mask].u_obj != mp_const_none) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("mask not supported yet"));
    }

    image_t img = py_esp32_image_make_image();
    int ksize = py_esp32_image_parse_ksize(args[1]);
    if (!omv_esp32_imlib_mean_filter(&img,
                                     ksize,
                                     parsed[ARG_threshold].u_bool,
                                     parsed[ARG_offset].u_int,
                                     parsed[ARG_invert].u_bool)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("mean failed"));
    }
    omv_esp32_imlib_gray_mark_dirty();
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
    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    if (parsed[ARG_mask].u_obj != mp_const_none) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("mask not supported yet"));
    }

    float percentile = (parsed[ARG_percentile].u_obj == mp_const_none) ? 0.5f
                                                                       : mp_obj_get_float_to_f(parsed[ARG_percentile].u_obj);
    if ((percentile < 0.0f) || (percentile > 1.0f)) {
        mp_raise_ValueError(MP_ERROR_TEXT("0 <= percentile <= 1"));
    }

    image_t img = py_esp32_image_make_image();
    int ksize = py_esp32_image_parse_ksize(args[1]);
    if (!omv_esp32_imlib_median_filter(&img,
                                       ksize,
                                       percentile,
                                       parsed[ARG_threshold].u_bool,
                                       parsed[ARG_offset].u_int,
                                       parsed[ARG_invert].u_bool)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("median failed"));
    }
    omv_esp32_imlib_gray_mark_dirty();
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
    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    if (parsed[ARG_mask].u_obj != mp_const_none) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("mask not supported yet"));
    }

    image_t img = py_esp32_image_make_image();
    int ksize = py_esp32_image_parse_ksize(args[1]);
    if (!omv_esp32_imlib_gaussian_filter(&img,
                                         ksize,
                                         parsed[ARG_unsharp].u_bool,
                                         parsed[ARG_threshold].u_bool,
                                         parsed[ARG_offset].u_int,
                                         parsed[ARG_invert].u_bool)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("gaussian failed"));
    }
    omv_esp32_imlib_gray_mark_dirty();
    return args[0];
}
static MP_DEFINE_CONST_FUN_OBJ_KW(py_esp32_image_gaussian_obj, 2, py_esp32_image_gaussian);

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
    mp_arg_parse_all(n_args - 2, args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    if (parsed[ARG_merge].u_bool) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("merge not supported yet"));
    }
    if (parsed[ARG_margin].u_int != 0) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("margin not supported yet"));
    }
    if ((parsed[ARG_x_stride].u_int != 1) || (parsed[ARG_y_stride].u_int != 1)) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("x_stride/y_stride must be 1"));
    }

    image_t img = py_esp32_image_make_image();
    rectangle_t roi;
    py_esp32_image_parse_roi(&img, parsed[ARG_roi].u_obj, &roi);

    omv_esp32_threshold_t *thresholds = NULL;
    size_t thresholds_len = py_esp32_image_parse_blob_thresholds(args[1], &thresholds);
    omv_esp32_blob_t *blobs = NULL;
    size_t blobs_len = 0;

    bool ok = omv_esp32_find_blobs(&img,
                                   &roi,
                                   thresholds,
                                   thresholds_len,
                                   parsed[ARG_invert].u_bool,
                                   parsed[ARG_area_threshold].u_int,
                                   parsed[ARG_pixels_threshold].u_int,
                                   &blobs,
                                   &blobs_len);
    py_esp32_image_free_blob_thresholds(thresholds, thresholds_len);

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
    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    image_t img = py_esp32_image_make_image();
    rectangle_t roi;
    py_esp32_image_parse_roi(&img, parsed[ARG_roi].u_obj, &roi);

    unsigned int r_max = parsed[ARG_r_max].u_int < 0 ? (unsigned int) IM_MIN(roi.w / 2, roi.h / 2)
                                                     : (unsigned int) parsed[ARG_r_max].u_int;
    find_circles_list_lnk_data_t *circles = NULL;
    size_t circles_len = 0;

    bool ok = omv_esp32_find_circles(&img,
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
    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);

    image_t img = py_esp32_image_make_image();
    rectangle_t roi;
    py_esp32_image_parse_roi(&img, parsed[ARG_roi].u_obj, &roi);

    find_rects_list_lnk_data_t *rects = NULL;
    size_t rects_len = 0;
    bool ok = omv_esp32_find_rects(&img, &roi, parsed[ARG_threshold].u_int, &rects, &rects_len);
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

static mp_obj_t py_esp32_image_flush(mp_obj_t self_in) {
    (void) self_in;
    image_t img = py_esp32_image_make_image();
    framebuffer_update_preview(&img);
    return self_in;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_flush_obj, py_esp32_image_flush);

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
    { MP_ROM_QSTR(MP_QSTR_get_pixel), MP_ROM_PTR(&py_esp32_image_get_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_pixel), MP_ROM_PTR(&py_esp32_image_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_line), MP_ROM_PTR(&py_esp32_image_draw_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_rectangle), MP_ROM_PTR(&py_esp32_image_draw_rectangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_cross), MP_ROM_PTR(&py_esp32_image_draw_cross_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_circle), MP_ROM_PTR(&py_esp32_image_draw_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_binary), MP_ROM_PTR(&py_esp32_image_binary_obj) },
    { MP_ROM_QSTR(MP_QSTR_mean), MP_ROM_PTR(&py_esp32_image_mean_obj) },
    { MP_ROM_QSTR(MP_QSTR_median), MP_ROM_PTR(&py_esp32_image_median_obj) },
    { MP_ROM_QSTR(MP_QSTR_gaussian), MP_ROM_PTR(&py_esp32_image_gaussian_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_edges), MP_ROM_PTR(&py_esp32_image_find_edges_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_blobs), MP_ROM_PTR(&py_esp32_image_find_blobs_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_circles), MP_ROM_PTR(&py_esp32_image_find_circles_obj) },
    { MP_ROM_QSTR(MP_QSTR_find_rects), MP_ROM_PTR(&py_esp32_image_find_rects_obj) },
    { MP_ROM_QSTR(MP_QSTR_invert), MP_ROM_PTR(&py_esp32_image_invert_obj) },
    { MP_ROM_QSTR(MP_QSTR_negate), MP_ROM_PTR(&py_esp32_image_invert_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&py_esp32_image_flush_obj) },
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
    return MP_OBJ_FROM_PTR(&py_esp32_image_obj);
}

static const mp_rom_map_elem_t py_esp32_image_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_image) },
    { MP_ROM_QSTR(MP_QSTR_EDGE_CANNY), MP_ROM_INT(EDGE_CANNY) },
    { MP_ROM_QSTR(MP_QSTR_EDGE_SIMPLE), MP_ROM_INT(EDGE_SIMPLE) },
    { MP_ROM_QSTR(MP_QSTR_Image), MP_ROM_PTR(&py_esp32_image_type) },
};
static MP_DEFINE_CONST_DICT(py_esp32_image_module_globals, py_esp32_image_module_globals_table);

const mp_obj_module_t image_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_t) &py_esp32_image_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_image, image_module);
