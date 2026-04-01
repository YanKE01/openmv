/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal blob detection subset for ESP32 bring-up.
 * This keeps the dependency surface much smaller than pulling in full blob.c.
 */

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "omv_imlib_blob_min.h"
#include "omv_imlib_gray_min.h"

typedef struct {
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    uint32_t pixels;
    uint32_t sum_x;
    uint32_t sum_y;
    bool used;
} omv_esp32_blob_accum_t;

static void *omv_esp32_blob_alloc_cleared(size_t size) {
    return heap_caps_calloc_prefer(1, size, 2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void omv_esp32_blob_free(void *ptr) {
    heap_caps_free(ptr);
}

static uint16_t omv_esp32_find_root(uint16_t *parents, uint16_t label) {
    while (parents[label] != label) {
        parents[label] = parents[parents[label]];
        label = parents[label];
    }
    return label;
}

static void omv_esp32_union_labels(uint16_t *parents, uint16_t a, uint16_t b) {
    uint16_t root_a = omv_esp32_find_root(parents, a);
    uint16_t root_b = omv_esp32_find_root(parents, b);

    if (root_a == root_b) {
        return;
    }

    if (root_a < root_b) {
        parents[root_b] = root_a;
    } else {
        parents[root_a] = root_b;
    }
}

static bool omv_esp32_threshold_match_pixel(uint16_t pixel,
                                            int y_value,
                                            const omv_esp32_threshold_t *thresholds,
                                            size_t thresholds_len,
                                            bool invert) {
    bool matched = false;

    for (size_t i = 0; i < thresholds_len; i++) {
        const omv_esp32_threshold_t *t = &thresholds[i];
        if (t->mode == OMV_ESP32_THRESHOLD_MODE_Y) {
            matched = (y_value >= t->l_lo) && (y_value <= t->l_hi);
        } else {
            int l = COLOR_RGB565_TO_L(pixel);
            int a = COLOR_RGB565_TO_A(pixel);
            int b = COLOR_RGB565_TO_B(pixel);
            matched = (l >= t->l_lo) && (l <= t->l_hi) &&
                      (a >= t->a_lo) && (a <= t->a_hi) &&
                      (b >= t->b_lo) && (b <= t->b_hi);
        }

        if (matched) {
            break;
        }
    }

    return invert ? !matched : matched;
}

bool omv_esp32_find_blobs(image_t *img,
                          rectangle_t *roi,
                          const omv_esp32_threshold_t *thresholds,
                          size_t thresholds_len,
                          bool invert,
                          unsigned int x_stride,
                          unsigned int y_stride,
                          unsigned int area_threshold,
                          unsigned int pixels_threshold,
                          omv_esp32_blob_t **out_blobs,
                          size_t *out_len) {
    if ((img == NULL) || (roi == NULL) || (thresholds == NULL) || (thresholds_len == 0) ||
        (out_blobs == NULL) || (out_len == NULL) || (img->pixfmt != PIXFORMAT_RGB565) ||
        (x_stride == 0) || (y_stride == 0)) {
        return false;
    }

    *out_blobs = NULL;
    *out_len = 0;

    int roi_x0 = IM_MAX(roi->x, 0);
    int roi_y0 = IM_MAX(roi->y, 0);
    int roi_x1 = IM_MIN(roi->x + roi->w, img->w);
    int roi_y1 = IM_MIN(roi->y + roi->h, img->h);

    if ((roi_x0 >= roi_x1) || (roi_y0 >= roi_y1)) {
        return true;
    }

    size_t roi_w = roi_x1 - roi_x0;
    size_t roi_h = roi_y1 - roi_y0;
    size_t roi_pixels = roi_w * roi_h;
    bool need_gray = false;

    for (size_t i = 0; i < thresholds_len; i++) {
        if (thresholds[i].mode == OMV_ESP32_THRESHOLD_MODE_Y) {
            need_gray = true;
            break;
        }
    }

    const uint8_t *gray = NULL;
    if (need_gray && !omv_esp32_imlib_gray_get(img, &gray)) {
        return false;
    }

    uint16_t *labels = omv_esp32_blob_alloc_cleared(roi_pixels * sizeof(uint16_t));
    uint16_t *parents = omv_esp32_blob_alloc_cleared((roi_pixels + 1) * sizeof(uint16_t));
    if ((labels == NULL) || (parents == NULL)) {
        omv_esp32_blob_free(labels);
        omv_esp32_blob_free(parents);
        return false;
    }

    uint16_t next_label = 1;

    for (int y = roi_y0; y < roi_y1; y += (int) y_stride) {
        uint16_t *row_ptr = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(img, y);
        for (int x = roi_x0; x < roi_x1; x += (int) x_stride) {
            uint16_t pixel = IMAGE_GET_RGB565_PIXEL_FAST(row_ptr, x);
            int y_value = need_gray ? gray[(y * img->w) + x] : COLOR_RGB565_TO_Y(pixel);
            if (!omv_esp32_threshold_match_pixel(pixel, y_value, thresholds, thresholds_len, invert)) {
                continue;
            }

            size_t idx = (size_t) (y - roi_y0) * roi_w + (size_t) (x - roi_x0);
            uint16_t neighbors[4] = {0};
            size_t neighbor_count = 0;

            if (x >= (roi_x0 + (int) x_stride)) {
                uint16_t left = labels[idx - x_stride];
                if (left != 0) {
                    neighbors[neighbor_count++] = left;
                }
            }
            if (y >= (roi_y0 + (int) y_stride)) {
                size_t up_idx = idx - (roi_w * y_stride);
                uint16_t up = labels[up_idx];
                if (up != 0) {
                    neighbors[neighbor_count++] = up;
                }
                if (x >= (roi_x0 + (int) x_stride)) {
                    uint16_t up_left = labels[up_idx - x_stride];
                    if (up_left != 0) {
                        neighbors[neighbor_count++] = up_left;
                    }
                }
                if ((x + (int) x_stride) < roi_x1) {
                    uint16_t up_right = labels[up_idx + x_stride];
                    if (up_right != 0) {
                        neighbors[neighbor_count++] = up_right;
                    }
                }
            }

            if (neighbor_count == 0) {
                if (next_label == UINT16_MAX) {
                    omv_esp32_blob_free(labels);
                    omv_esp32_blob_free(parents);
                    return false;
                }
                labels[idx] = next_label;
                parents[next_label] = next_label;
                next_label += 1;
            } else {
                uint16_t min_label = neighbors[0];
                for (size_t i = 1; i < neighbor_count; i++) {
                    if (neighbors[i] < min_label) {
                        min_label = neighbors[i];
                    }
                }
                labels[idx] = min_label;
                for (size_t i = 0; i < neighbor_count; i++) {
                    omv_esp32_union_labels(parents, min_label, neighbors[i]);
                }
            }
        }

        if ((((y - roi_y0) / (int) y_stride) & 0x1f) == 0) {
            vTaskDelay(1);
        }
    }

    size_t label_count = next_label;
    omv_esp32_blob_accum_t *accum = omv_esp32_blob_alloc_cleared(label_count * sizeof(omv_esp32_blob_accum_t));
    if (accum == NULL) {
        omv_esp32_blob_free(labels);
        omv_esp32_blob_free(parents);
        return false;
    }

    for (int y = roi_y0; y < roi_y1; y += (int) y_stride) {
        for (int x = roi_x0; x < roi_x1; x += (int) x_stride) {
            size_t idx = (size_t) (y - roi_y0) * roi_w + (size_t) (x - roi_x0);
            uint16_t label = labels[idx];
            if (label == 0) {
                continue;
            }

            uint16_t root = omv_esp32_find_root(parents, label);
            labels[idx] = root;
            omv_esp32_blob_accum_t *a = &accum[root];

            if (!a->used) {
                a->used = true;
                a->x0 = x;
                a->y0 = y;
                a->x1 = x;
                a->y1 = y;
            } else {
                a->x0 = IM_MIN(a->x0, x);
                a->y0 = IM_MIN(a->y0, y);
                a->x1 = IM_MAX(a->x1, x);
                a->y1 = IM_MAX(a->y1, y);
            }

            a->pixels += 1;
            a->sum_x += x;
            a->sum_y += y;
        }

        if ((((y - roi_y0) / (int) y_stride) & 0x1f) == 0) {
            vTaskDelay(1);
        }
    }

    size_t valid_count = 0;
    for (size_t i = 1; i < label_count; i++) {
        if (!accum[i].used) {
            continue;
        }
        unsigned int w = (unsigned int) (accum[i].x1 - accum[i].x0 + 1);
        unsigned int h = (unsigned int) (accum[i].y1 - accum[i].y0 + 1);
        unsigned int area = w * h;
        if ((area >= area_threshold) && (accum[i].pixels >= pixels_threshold)) {
            valid_count += 1;
        }
    }

    if (valid_count != 0) {
        omv_esp32_blob_t *blobs = omv_esp32_blob_alloc_cleared(valid_count * sizeof(omv_esp32_blob_t));
        if (blobs == NULL) {
            omv_esp32_blob_free(accum);
            omv_esp32_blob_free(parents);
            omv_esp32_blob_free(labels);
            return false;
        }
        size_t out_index = 0;
        for (size_t i = 1; i < label_count; i++) {
            if (!accum[i].used) {
                continue;
            }
            int w = accum[i].x1 - accum[i].x0 + 1;
            int h = accum[i].y1 - accum[i].y0 + 1;
            int area = w * h;
            if ((area < (int) area_threshold) || (accum[i].pixels < pixels_threshold)) {
                continue;
            }

            blobs[out_index].x = accum[i].x0;
            blobs[out_index].y = accum[i].y0;
            blobs[out_index].w = w;
            blobs[out_index].h = h;
            blobs[out_index].pixels = accum[i].pixels;
            blobs[out_index].cx = ((float) accum[i].sum_x) / accum[i].pixels;
            blobs[out_index].cy = ((float) accum[i].sum_y) / accum[i].pixels;
            blobs[out_index].code = 1;
            blobs[out_index].count = 1;
            out_index += 1;
        }
        *out_blobs = blobs;
        *out_len = valid_count;
    }

    omv_esp32_blob_free(accum);
    omv_esp32_blob_free(parents);
    omv_esp32_blob_free(labels);
    return true;
}

void omv_esp32_free_blobs(omv_esp32_blob_t *blobs) {
    omv_esp32_blob_free(blobs);
}
