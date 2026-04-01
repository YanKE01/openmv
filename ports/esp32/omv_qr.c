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
#include "quirc.h"

#include "framebuffer.h"
#include "omv_qr.h"

typedef struct {
    struct quirc *decoder;
    struct quirc_code *code;
    struct quirc_data *data;
    int width;
    int height;
} omv_qr_ctx_t;

static omv_qr_ctx_t omv_qr_ctx;

static void *omv_qr_malloc(size_t size) {
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    return ptr;
}

static bool omv_qr_ensure_decode_buffers(void) {
    if (omv_qr_ctx.code == NULL) {
        omv_qr_ctx.code = omv_qr_malloc(sizeof(struct quirc_code));
        if (omv_qr_ctx.code == NULL) {
            return false;
        }
    }

    if (omv_qr_ctx.data == NULL) {
        omv_qr_ctx.data = omv_qr_malloc(sizeof(struct quirc_data));
        if (omv_qr_ctx.data == NULL) {
            heap_caps_free(omv_qr_ctx.code);
            omv_qr_ctx.code = NULL;
            return false;
        }
    }

    return true;
}

static bool omv_qr_ensure_decoder(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (omv_qr_ctx.decoder == NULL) {
        omv_qr_ctx.decoder = quirc_new();
        if (omv_qr_ctx.decoder == NULL) {
            return false;
        }
        omv_qr_ctx.width = 0;
        omv_qr_ctx.height = 0;
    }

    if ((omv_qr_ctx.width != width) || (omv_qr_ctx.height != height)) {
        if (quirc_resize(omv_qr_ctx.decoder, width, height) < 0) {
            return false;
        }
        omv_qr_ctx.width = width;
        omv_qr_ctx.height = height;
    }

    return true;
}

static void omv_qr_rgb565_roi_to_gray(const uint16_t *src, int src_w, const rectangle_t *roi, uint8_t *dst) {
    for (int y = 0; y < roi->h; y++) {
        const uint16_t *src_row = src + ((roi->y + y) * src_w) + roi->x;
        uint8_t *dst_row = dst + ((size_t) y * roi->w);

        for (int x = 0; x < roi->w; x++) {
            uint16_t pixel = src_row[x];
            uint32_t r = (pixel >> 11) & 0x1F;
            uint32_t g = (pixel >> 5) & 0x3F;
            uint32_t b = pixel & 0x1F;

            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            dst_row[x] = (uint8_t) ((77 * r + 150 * g + 29 * b) >> 8);
        }

        if ((y & 0x1F) == 0x1F) {
            vTaskDelay(1);
        }
    }
}

static void omv_qr_fill_geometry(omv_qrcode_t *dst, const struct quirc_code *code, const rectangle_t *roi) {
    int min_x = roi->x + code->corners[0].x;
    int min_y = roi->y + code->corners[0].y;
    int max_x = min_x;
    int max_y = min_y;

    for (int i = 0; i < 4; i++) {
        int x = roi->x + code->corners[i].x;
        int y = roi->y + code->corners[i].y;

        dst->corners[i].x = x;
        dst->corners[i].y = y;
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
    }

    dst->rect.x = min_x;
    dst->rect.y = min_y;
    dst->rect.w = max_x - min_x + 1;
    dst->rect.h = max_y - min_y + 1;
}

void omv_qr_free_qrcodes(omv_qrcode_list_t *out) {
    if (out == NULL) {
        return;
    }

    for (size_t i = 0; i < out->count; i++) {
        heap_caps_free(out->items[i].payload);
    }

    heap_caps_free(out->items);
    out->items = NULL;
    out->count = 0;
}

bool omv_qr_find_qrcodes(omv_qrcode_list_t *out, const rectangle_t *roi) {
    framebuffer_t *fb = framebuffer_get(FB_MAINFB_ID);
    rectangle_t full_roi;
    const rectangle_t *scan_roi = roi;
    uint8_t *gray = NULL;
    int found_count = 0;
    size_t valid_count = 0;
    size_t out_index = 0;

    if ((out == NULL) || (fb == NULL) || (fb->raw_base == NULL) || (fb->pixfmt != PIXFORMAT_RGB565)) {
        return false;
    }

    if (scan_roi == NULL) {
        full_roi.x = 0;
        full_roi.y = 0;
        full_roi.w = fb->w;
        full_roi.h = fb->h;
        scan_roi = &full_roi;
    }

    if ((scan_roi->x < 0) || (scan_roi->y < 0) ||
        (scan_roi->w <= 0) || (scan_roi->h <= 0) ||
        ((scan_roi->x + scan_roi->w) > fb->w) ||
        ((scan_roi->y + scan_roi->h) > fb->h)) {
        return false;
    }

    out->count = 0;
    out->items = NULL;

    if (!omv_qr_ensure_decoder(scan_roi->w, scan_roi->h)) {
        return false;
    }
    if (!omv_qr_ensure_decode_buffers()) {
        return false;
    }

    gray = quirc_begin(omv_qr_ctx.decoder, NULL, NULL);
    if (gray == NULL) {
        return false;
    }

    omv_qr_rgb565_roi_to_gray((const uint16_t *) fb->raw_base, fb->w, scan_roi, gray);
    quirc_end(omv_qr_ctx.decoder);

    found_count = quirc_count(omv_qr_ctx.decoder);
    for (int i = 0; i < found_count; i++) {
        quirc_extract(omv_qr_ctx.decoder, i, omv_qr_ctx.code);
        if (quirc_decode(omv_qr_ctx.code, omv_qr_ctx.data) == QUIRC_SUCCESS) {
            valid_count++;
        }
    }

    if (valid_count == 0) {
        return true;
    }

    out->items = omv_qr_malloc(valid_count * sizeof(omv_qrcode_t));
    if (out->items == NULL) {
        return false;
    }
    memset(out->items, 0, valid_count * sizeof(omv_qrcode_t));
    out->count = valid_count;

    for (int i = 0; i < found_count; i++) {
        omv_qrcode_t *dst;

        quirc_extract(omv_qr_ctx.decoder, i, omv_qr_ctx.code);
        if (quirc_decode(omv_qr_ctx.code, omv_qr_ctx.data) != QUIRC_SUCCESS) {
            continue;
        }

        dst = &out->items[out_index];
        dst->payload = omv_qr_malloc((size_t) omv_qr_ctx.data->payload_len + 1);
        if (dst->payload == NULL) {
            omv_qr_free_qrcodes(out);
            return false;
        }

        if (omv_qr_ctx.data->payload_len > 0) {
            memcpy(dst->payload, omv_qr_ctx.data->payload, (size_t) omv_qr_ctx.data->payload_len);
        }
        dst->payload[omv_qr_ctx.data->payload_len] = '\0';
        dst->payload_len = (size_t) omv_qr_ctx.data->payload_len;
        dst->version = (uint8_t) omv_qr_ctx.data->version;
        dst->ecc_level = (uint8_t) omv_qr_ctx.data->ecc_level;
        dst->mask = (uint8_t) omv_qr_ctx.data->mask;
        dst->data_type = (uint8_t) omv_qr_ctx.data->data_type;
        dst->eci = omv_qr_ctx.data->eci;
        omv_qr_fill_geometry(dst, omv_qr_ctx.code, scan_roi);
        out_index++;
    }

    return true;
}
