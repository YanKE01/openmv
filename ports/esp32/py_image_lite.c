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

static py_esp32_image_obj_t py_esp32_image_obj;
static void py_barcode_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest);

static framebuffer_t *py_esp32_image_fb(void) {
    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);

    if ((fb == NULL) || (fb->raw_base == NULL)) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("framebuffer unavailable"));
    }

    return fb;
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
    framebuffer_t *fb = py_esp32_image_fb();
    image_t image;

    framebuffer_to_image(fb, &image);
    framebuffer_update_preview(&image);
    return self_in;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_esp32_image_flush_obj, py_esp32_image_flush);

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
    { MP_ROM_QSTR(MP_QSTR_find_barcodes), MP_ROM_PTR(&py_esp32_image_find_barcodes_obj) },
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
};
static MP_DEFINE_CONST_DICT(py_esp32_image_module_globals, py_esp32_image_module_globals_table);

const mp_obj_module_t image_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *) &py_esp32_image_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_image, image_module);
