/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal OpenMV-style image filters for the ESP32 bring-up path.
 * This file intentionally implements a small RGB565-only subset instead
 * of pulling the full imlib filter dependency graph into the port.
 */

#include <stdlib.h>
#include <string.h>

#include "omv_imlib_filter_min.h"

static inline int omv_esp32_imlib_clamp_i32(int value, int low, int high) {
    return IM_MIN(IM_MAX(value, low), high);
}

static inline uint16_t omv_esp32_imlib_get_rgb565_clamped(const image_t *img, int x, int y) {
    int clamped_y = omv_esp32_imlib_clamp_i32(y, 0, img->h - 1);
    int clamped_x = omv_esp32_imlib_clamp_i32(x, 0, img->w - 1);
    uint16_t *row = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(img, clamped_y);
    return IMAGE_GET_RGB565_PIXEL_FAST(row, clamped_x);
}

static inline uint16_t omv_esp32_imlib_threshold_rgb565(uint16_t filtered, uint16_t original,
                                                         bool threshold, int offset, bool invert) {
    if (!threshold) {
        return filtered;
    }

    bool is_binary_max = (((COLOR_RGB565_TO_Y(filtered) - offset) < COLOR_RGB565_TO_Y(original)) ^ invert);
    return is_binary_max ? COLOR_RGB565_BINARY_MAX : COLOR_RGB565_BINARY_MIN;
}

static inline void omv_esp32_imlib_store_rgb565(uint16_t *dst, int x, uint16_t pixel) {
    IMAGE_PUT_RGB565_PIXEL_FAST(dst, x, pixel);
}

static bool omv_esp32_imlib_alloc_rgb565_pair(const image_t *img, uint16_t **tmp0, uint16_t **tmp1) {
    size_t size = (size_t) img->w * (size_t) img->h * sizeof(uint16_t);
    *tmp0 = malloc(size);
    *tmp1 = malloc(size);
    if ((*tmp0 == NULL) || (*tmp1 == NULL)) {
        free(*tmp1);
        free(*tmp0);
        *tmp0 = NULL;
        *tmp1 = NULL;
        return false;
    }
    return true;
}

static void omv_esp32_imlib_mean_horizontal_pass(const image_t *src, uint16_t *dst, int radius) {
    int side = (radius * 2) + 1;
    for (int y = 0; y < src->h; y++) {
        uint16_t *dst_row = dst + (y * src->w);
        int r_acc = 0;
        int g_acc = 0;
        int b_acc = 0;

        for (int kx = -radius; kx <= radius; kx++) {
            uint16_t pixel = omv_esp32_imlib_get_rgb565_clamped(src, kx, y);
            r_acc += COLOR_RGB565_TO_R5(pixel);
            g_acc += COLOR_RGB565_TO_G6(pixel);
            b_acc += COLOR_RGB565_TO_B5(pixel);
        }

        for (int x = 0; x < src->w; x++) {
            dst_row[x] = COLOR_R5_G6_B5_TO_RGB565(r_acc / side, g_acc / side, b_acc / side);

            if (x == (src->w - 1)) {
                continue;
            }

            uint16_t remove_pixel = omv_esp32_imlib_get_rgb565_clamped(src, x - radius, y);
            uint16_t add_pixel = omv_esp32_imlib_get_rgb565_clamped(src, x + radius + 1, y);
            r_acc += COLOR_RGB565_TO_R5(add_pixel) - COLOR_RGB565_TO_R5(remove_pixel);
            g_acc += COLOR_RGB565_TO_G6(add_pixel) - COLOR_RGB565_TO_G6(remove_pixel);
            b_acc += COLOR_RGB565_TO_B5(add_pixel) - COLOR_RGB565_TO_B5(remove_pixel);
        }
    }
}

static void omv_esp32_imlib_mean_vertical_pass(const image_t *src, const uint16_t *tmp, uint16_t *dst,
                                               int radius, bool threshold, int offset, bool invert) {
    int side = (radius * 2) + 1;

    for (int x = 0; x < src->w; x++) {
        int r_acc = 0;
        int g_acc = 0;
        int b_acc = 0;

        for (int ky = -radius; ky <= radius; ky++) {
            int sample_y = omv_esp32_imlib_clamp_i32(ky, 0, src->h - 1);
            uint16_t pixel = tmp[(sample_y * src->w) + x];
            r_acc += COLOR_RGB565_TO_R5(pixel);
            g_acc += COLOR_RGB565_TO_G6(pixel);
            b_acc += COLOR_RGB565_TO_B5(pixel);
        }

        for (int y = 0; y < src->h; y++) {
            uint16_t *src_row = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(src, y);
            uint16_t filtered = COLOR_R5_G6_B5_TO_RGB565(r_acc / side, g_acc / side, b_acc / side);
            dst[(y * src->w) + x] = omv_esp32_imlib_threshold_rgb565(filtered,
                                                                     IMAGE_GET_RGB565_PIXEL_FAST(src_row, x),
                                                                     threshold,
                                                                     offset,
                                                                     invert);

            if (y == (src->h - 1)) {
                continue;
            }

            int remove_y = omv_esp32_imlib_clamp_i32(y - radius, 0, src->h - 1);
            int add_y = omv_esp32_imlib_clamp_i32(y + radius + 1, 0, src->h - 1);
            uint16_t remove_pixel = tmp[(remove_y * src->w) + x];
            uint16_t add_pixel = tmp[(add_y * src->w) + x];
            r_acc += COLOR_RGB565_TO_R5(add_pixel) - COLOR_RGB565_TO_R5(remove_pixel);
            g_acc += COLOR_RGB565_TO_G6(add_pixel) - COLOR_RGB565_TO_G6(remove_pixel);
            b_acc += COLOR_RGB565_TO_B5(add_pixel) - COLOR_RGB565_TO_B5(remove_pixel);
        }
    }
}

static void omv_esp32_imlib_gaussian_horizontal_pass(const image_t *src, uint16_t *dst,
                                                     const int *weights, int radius, int weight_sum) {
    int side = (radius * 2) + 1;
    for (int y = 0; y < src->h; y++) {
        uint16_t *dst_row = dst + (y * src->w);
        for (int x = 0; x < src->w; x++) {
            long r_acc = 0;
            long g_acc = 0;
            long b_acc = 0;

            for (int kx = -radius; kx <= radius; kx++) {
                uint16_t pixel = omv_esp32_imlib_get_rgb565_clamped(src, x + kx, y);
                int weight = weights[kx + radius];
                r_acc += COLOR_RGB565_TO_R5(pixel) * weight;
                g_acc += COLOR_RGB565_TO_G6(pixel) * weight;
                b_acc += COLOR_RGB565_TO_B5(pixel) * weight;
            }

            dst_row[x] = COLOR_R5_G6_B5_TO_RGB565((int) (r_acc / weight_sum),
                                                  (int) (g_acc / weight_sum),
                                                  (int) (b_acc / weight_sum));
        }
    }
    (void) side;
}

static void omv_esp32_imlib_gaussian_vertical_pass(const image_t *src, const uint16_t *tmp, uint16_t *dst,
                                                   const int *weights, int radius, int weight_sum,
                                                   bool unsharp, bool threshold, int offset, bool invert) {
    for (int x = 0; x < src->w; x++) {
        for (int y = 0; y < src->h; y++) {
            long r_acc = 0;
            long g_acc = 0;
            long b_acc = 0;

            for (int ky = -radius; ky <= radius; ky++) {
                int sample_y = omv_esp32_imlib_clamp_i32(y + ky, 0, src->h - 1);
                uint16_t pixel = tmp[(sample_y * src->w) + x];
                int weight = weights[ky + radius];
                r_acc += COLOR_RGB565_TO_R5(pixel) * weight;
                g_acc += COLOR_RGB565_TO_G6(pixel) * weight;
                b_acc += COLOR_RGB565_TO_B5(pixel) * weight;
            }

            int r = (int) (r_acc / weight_sum);
            int g = (int) (g_acc / weight_sum);
            int b = (int) (b_acc / weight_sum);

            if (unsharp) {
                uint16_t *src_row = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(src, y);
                uint16_t original = IMAGE_GET_RGB565_PIXEL_FAST(src_row, x);
                r = omv_esp32_imlib_clamp_i32((COLOR_RGB565_TO_R5(original) * 2) - r, COLOR_R5_MIN, COLOR_R5_MAX);
                g = omv_esp32_imlib_clamp_i32((COLOR_RGB565_TO_G6(original) * 2) - g, COLOR_G6_MIN, COLOR_G6_MAX);
                b = omv_esp32_imlib_clamp_i32((COLOR_RGB565_TO_B5(original) * 2) - b, COLOR_B5_MIN, COLOR_B5_MAX);
            }

            uint16_t *src_row = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(src, y);
            uint16_t filtered = COLOR_R5_G6_B5_TO_RGB565(r, g, b);
            dst[(y * src->w) + x] = omv_esp32_imlib_threshold_rgb565(filtered,
                                                                     IMAGE_GET_RGB565_PIXEL_FAST(src_row, x),
                                                                     threshold,
                                                                     offset,
                                                                     invert);
        }
    }
}

static void omv_esp32_imlib_sort_u8(uint8_t *data, int len) {
    for (int i = 1; i < len; i++) {
        uint8_t value = data[i];
        int j = i - 1;
        while ((j >= 0) && (data[j] > value)) {
            data[j + 1] = data[j];
            j--;
        }
        data[j + 1] = value;
    }
}

static bool omv_esp32_imlib_validate_filter_input(const image_t *img, int ksize) {
    return (img != NULL) &&
           (img->pixfmt == PIXFORMAT_RGB565) &&
           (img->pixels != NULL) &&
           (img->w > 0) &&
           (img->h > 0) &&
           (ksize >= 0);
}

bool omv_esp32_imlib_mean_filter(image_t *img, int ksize, bool threshold, int offset, bool invert) {
    if (!omv_esp32_imlib_validate_filter_input(img, ksize)) {
        return false;
    }

    size_t size = (size_t) img->w * (size_t) img->h * sizeof(uint16_t);
    uint16_t *tmp = NULL;
    uint16_t *dst = NULL;
    if (!omv_esp32_imlib_alloc_rgb565_pair(img, &tmp, &dst)) {
        return false;
    }

    omv_esp32_imlib_mean_horizontal_pass(img, tmp, ksize);
    omv_esp32_imlib_mean_vertical_pass(img, tmp, dst, ksize, threshold, offset, invert);

    memcpy(img->pixels, dst, size);
    free(dst);
    free(tmp);
    return true;
}

bool omv_esp32_imlib_median_filter(image_t *img, int ksize, float percentile, bool threshold, int offset, bool invert) {
    if (!omv_esp32_imlib_validate_filter_input(img, ksize)) {
        return false;
    }
    if ((percentile < 0.0f) || (percentile > 1.0f)) {
        return false;
    }

    size_t size = (size_t) img->w * (size_t) img->h * sizeof(uint16_t);
    uint16_t *dst = malloc(size);
    if (dst == NULL) {
        return false;
    }

    int radius = ksize;
    int side = (radius * 2) + 1;
    int kernel_area = side * side;
    uint8_t *r_values = malloc(kernel_area);
    uint8_t *g_values = malloc(kernel_area);
    uint8_t *b_values = malloc(kernel_area);

    if ((r_values == NULL) || (g_values == NULL) || (b_values == NULL)) {
        free(b_values);
        free(g_values);
        free(r_values);
        free(dst);
        return false;
    }

    int index = (int) fast_floorf(percentile * (kernel_area - 1));
    index = omv_esp32_imlib_clamp_i32(index, 0, kernel_area - 1);

    for (int y = 0; y < img->h; y++) {
        uint16_t *dst_row = dst + (y * img->w);
        uint16_t *src_row = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(img, y);

        for (int x = 0; x < img->w; x++) {
            int n = 0;

            for (int ky = -radius; ky <= radius; ky++) {
                for (int kx = -radius; kx <= radius; kx++) {
                    uint16_t pixel = omv_esp32_imlib_get_rgb565_clamped(img, x + kx, y + ky);
                    r_values[n] = COLOR_RGB565_TO_R5(pixel);
                    g_values[n] = COLOR_RGB565_TO_G6(pixel);
                    b_values[n] = COLOR_RGB565_TO_B5(pixel);
                    n++;
                }
            }

            omv_esp32_imlib_sort_u8(r_values, n);
            omv_esp32_imlib_sort_u8(g_values, n);
            omv_esp32_imlib_sort_u8(b_values, n);

            uint16_t filtered = COLOR_R5_G6_B5_TO_RGB565(r_values[index], g_values[index], b_values[index]);
            uint16_t output = omv_esp32_imlib_threshold_rgb565(filtered,
                                                               IMAGE_GET_RGB565_PIXEL_FAST(src_row, x),
                                                               threshold,
                                                               offset,
                                                               invert);
            omv_esp32_imlib_store_rgb565(dst_row, x, output);
        }
    }

    memcpy(img->pixels, dst, size);
    free(b_values);
    free(g_values);
    free(r_values);
    free(dst);
    return true;
}

bool omv_esp32_imlib_gaussian_filter(image_t *img, int ksize, bool unsharp, bool threshold, int offset, bool invert) {
    if (!omv_esp32_imlib_validate_filter_input(img, ksize)) {
        return false;
    }

    size_t size = (size_t) img->w * (size_t) img->h * sizeof(uint16_t);
    uint16_t *tmp = NULL;
    uint16_t *dst = NULL;
    if (!omv_esp32_imlib_alloc_rgb565_pair(img, &tmp, &dst)) {
        return false;
    }

    int radius = ksize;
    int side = (radius * 2) + 1;
    int *pascal = malloc(sizeof(int) * side);
    int *kernel = malloc(sizeof(int) * side * side);

    if ((pascal == NULL) || (kernel == NULL)) {
        free(kernel);
        free(pascal);
        free(dst);
        free(tmp);
        return false;
    }

    pascal[0] = 1;
    for (int i = 0; i < (radius * 2); i++) {
        pascal[i + 1] = (pascal[i] * ((radius * 2) - i)) / (i + 1);
    }

    int sum = 0;
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            int value = pascal[y] * pascal[x];
            kernel[(y * side) + x] = value;
            sum += value;
        }
    }

    if (unsharp) {
        kernel[(side * side) / 2] -= sum * 2;
        sum = -sum;
    }
    if (sum == 0) {
        sum = 1;
    }

    omv_esp32_imlib_gaussian_horizontal_pass(img, tmp, pascal, radius, sum);
    omv_esp32_imlib_gaussian_vertical_pass(img, tmp, dst, pascal, radius, sum, unsharp, threshold, offset, invert);

    memcpy(img->pixels, dst, size);
    free(kernel);
    free(pascal);
    free(dst);
    free(tmp);
    return true;
}
