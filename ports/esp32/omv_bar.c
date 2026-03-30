/*
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zbar.h"

#include "framebuffer.h"
#include "imlib.h"
#include "omv_bar.h"

typedef struct {
    zbar_image_scanner_t *scanner;
    uint8_t *gray;
    size_t gray_pixels;
} omv_bar_ctx_t;

static omv_bar_ctx_t omv_bar_ctx;

static void *omv_bar_malloc(size_t size) {
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    return ptr;
}

static bool omv_bar_ensure_buffers(size_t pixel_count) {
    if (pixel_count == 0) {
        return false;
    }

    if (omv_bar_ctx.gray_pixels < pixel_count) {
        heap_caps_free(omv_bar_ctx.gray);
        omv_bar_ctx.gray = omv_bar_malloc(pixel_count);
        if (omv_bar_ctx.gray == NULL) {
            omv_bar_ctx.gray_pixels = 0;
            return false;
        }
        omv_bar_ctx.gray_pixels = pixel_count;
    }

    return true;
}

static bool omv_bar_ensure_scanner(void) {
    if (omv_bar_ctx.scanner != NULL) {
        return true;
    }

    omv_bar_ctx.scanner = zbar_image_scanner_create();
    if (omv_bar_ctx.scanner == NULL) {
        return false;
    }

    zbar_image_scanner_enable_cache(omv_bar_ctx.scanner, 0);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_NONE, ZBAR_CFG_ENABLE, 0);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_NONE, ZBAR_CFG_POSITION, 1);

    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_EAN2, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_EAN5, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_EAN8, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_UPCE, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_ISBN10, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_UPCA, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_EAN13, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_ISBN13, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_I25, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_DATABAR, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_DATABAR_EXP, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_CODABAR, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_CODE39, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_PDF417, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_CODE93, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(omv_bar_ctx.scanner, ZBAR_CODE128, ZBAR_CFG_ENABLE, 1);

    return true;
}

static void omv_bar_rgb565_to_gray(const uint16_t *src, uint8_t *dst, uint32_t width, uint32_t height) {
    for (uint32_t y = 0; y < height; y++) {
        size_t row_offset = (size_t) y * width;

        for (uint32_t x = 0; x < width; x++) {
            uint16_t pixel = src[row_offset + x];
            uint32_t r = (pixel >> 11) & 0x1F;
            uint32_t g = (pixel >> 5) & 0x3F;
            uint32_t b = pixel & 0x1F;

            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            dst[row_offset + x] = (uint8_t) ((77 * r + 150 * g + 29 * b) >> 8);
        }

        if ((y & 0x1F) == 0x1F) {
            vTaskDelay(1);
        }
    }
}

static int omv_bar_map_type(zbar_symbol_type_t type) {
    switch (type) {
        case ZBAR_EAN2: return BARCODE_EAN2;
        case ZBAR_EAN5: return BARCODE_EAN5;
        case ZBAR_EAN8: return BARCODE_EAN8;
        case ZBAR_UPCE: return BARCODE_UPCE;
        case ZBAR_ISBN10: return BARCODE_ISBN10;
        case ZBAR_UPCA: return BARCODE_UPCA;
        case ZBAR_EAN13: return BARCODE_EAN13;
        case ZBAR_ISBN13: return BARCODE_ISBN13;
        case ZBAR_I25: return BARCODE_I25;
        case ZBAR_DATABAR: return BARCODE_DATABAR;
        case ZBAR_DATABAR_EXP: return BARCODE_DATABAR_EXP;
        case ZBAR_CODABAR: return BARCODE_CODABAR;
        case ZBAR_CODE39: return BARCODE_CODE39;
        case ZBAR_PDF417: return BARCODE_PDF417;
        case ZBAR_CODE93: return BARCODE_CODE93;
        case ZBAR_CODE128: return BARCODE_CODE128;
        default: return -1;
    }
}

static uint16_t omv_bar_map_rotation(zbar_orientation_t orientation) {
    switch (orientation) {
        case ZBAR_ORIENT_RIGHT: return 270;
        case ZBAR_ORIENT_DOWN: return 180;
        case ZBAR_ORIENT_LEFT: return 90;
        case ZBAR_ORIENT_UP:
        case ZBAR_ORIENT_UNKNOWN:
        default: return 0;
    }
}

static void omv_bar_fill_geometry(omv_barcode_t *dst, const zbar_symbol_t *symbol) {
    unsigned loc_size = zbar_symbol_get_loc_size(symbol);
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;

    if (loc_size > 0) {
        min_x = max_x = zbar_symbol_get_loc_x(symbol, 0);
        min_y = max_y = zbar_symbol_get_loc_y(symbol, 0);

        for (unsigned i = 0; i < loc_size; i++) {
            int x = zbar_symbol_get_loc_x(symbol, i);
            int y = zbar_symbol_get_loc_y(symbol, i);

            if (x < min_x) {
                min_x = x;
            }
            if (x > max_x) {
                max_x = x;
            }
            if (y < min_y) {
                min_y = y;
            }
            if (y > max_y) {
                max_y = y;
            }

            if (i < 4) {
                dst->corners[i].x = x;
                dst->corners[i].y = y;
            }
        }
    }

    dst->rect.x = min_x;
    dst->rect.y = min_y;
    dst->rect.w = (loc_size > 0) ? (max_x - min_x + 1) : 0;
    dst->rect.h = (loc_size > 0) ? (max_y - min_y + 1) : 0;

    if (loc_size < 4) {
        dst->corners[0].x = min_x;
        dst->corners[0].y = min_y;
        dst->corners[1].x = max_x;
        dst->corners[1].y = min_y;
        dst->corners[2].x = max_x;
        dst->corners[2].y = max_y;
        dst->corners[3].x = min_x;
        dst->corners[3].y = max_y;
    }
}

void omv_bar_free_barcodes(omv_barcode_list_t *out) {
    if (out == NULL) {
        return;
    }

    for (size_t i = 0; i < out->count; i++) {
        free(out->items[i].payload);
    }

    free(out->items);
    out->items = NULL;
    out->count = 0;
}

bool omv_bar_find_barcodes(omv_barcode_list_t *out) {
    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);
    uint32_t width;
    uint32_t height;
    size_t pixel_count;
    zbar_image_t *image = NULL;
    const zbar_symbol_t *symbol;
    size_t count = 0;
    size_t index = 0;

    if (out == NULL || fb == NULL || fb->raw_base == NULL || fb->pixfmt != PIXFORMAT_RGB565) {
        return false;
    }

    width = fb->w;
    height = fb->h;
    pixel_count = (size_t) width * (size_t) height;
    if (width == 0 || height == 0 || fb->size < (pixel_count * sizeof(uint16_t))) {
        return false;
    }

    out->count = 0;
    out->items = NULL;

    if (!omv_bar_ensure_scanner() || !omv_bar_ensure_buffers(pixel_count)) {
        return false;
    }

    omv_bar_rgb565_to_gray((const uint16_t *) fb->raw_base, omv_bar_ctx.gray, width, height);

    image = zbar_image_create();
    if (image == NULL) {
        return false;
    }

    zbar_image_set_format(image, zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image, width, height);
    zbar_image_set_data(image, omv_bar_ctx.gray, pixel_count, NULL);

    if (zbar_scan_image(omv_bar_ctx.scanner, image) < 0) {
        zbar_image_destroy(image);
        return false;
    }

    for (symbol = zbar_image_first_symbol(image); symbol != NULL; symbol = zbar_symbol_next(symbol)) {
        if (omv_bar_map_type(zbar_symbol_get_type(symbol)) >= 0) {
            count++;
        }
    }

    if (count == 0) {
        zbar_image_destroy(image);
        return true;
    }

    out->items = calloc(count, sizeof(omv_barcode_t));
    if (out->items == NULL) {
        zbar_image_destroy(image);
        return false;
    }

    out->count = count;
    for (symbol = zbar_image_first_symbol(image); symbol != NULL; symbol = zbar_symbol_next(symbol)) {
        int type = omv_bar_map_type(zbar_symbol_get_type(symbol));
        unsigned payload_len;

        if (type < 0) {
            continue;
        }

        payload_len = zbar_symbol_get_data_length(symbol);
        out->items[index].payload = malloc(payload_len + 1);
        if (out->items[index].payload == NULL) {
            zbar_image_destroy(image);
            omv_bar_free_barcodes(out);
            return false;
        }

        if (payload_len) {
            memcpy(out->items[index].payload, zbar_symbol_get_data(symbol), payload_len);
        }
        out->items[index].payload[payload_len] = '\0';

        out->items[index].payload_len = payload_len;
        out->items[index].type = (uint16_t) type;
        out->items[index].rotation = omv_bar_map_rotation(zbar_symbol_get_orientation(symbol));
        out->items[index].quality = zbar_symbol_get_quality(symbol);
        omv_bar_fill_geometry(&out->items[index], symbol);
        index++;
    }

    zbar_image_destroy(image);
    return true;
}
