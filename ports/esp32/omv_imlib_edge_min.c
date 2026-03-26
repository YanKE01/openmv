/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal edge detection helpers for the ESP32 bring-up path.
 */

#include <stdlib.h>
#include <string.h>

#include "omv_imlib_gray_min.h"
#include "omv_imlib_edge_min.h"

typedef struct _omv_esp32_edge_grad_t {
    uint16_t t;
    uint16_t g;
} omv_esp32_edge_grad_t;

static omv_esp32_edge_grad_t *omv_esp32_edge_grad_buf = NULL;
static size_t omv_esp32_edge_grad_buf_cap = 0;

static inline int omv_esp32_edge_clamp_i32(int value, int low, int high) {
    return IM_MIN(IM_MAX(value, low), high);
}

static inline int omv_esp32_edge_abs_i32(int value) {
    return (value < 0) ? -value : value;
}

static inline uint16_t omv_esp32_edge_grad_mag_fast(int gx, int gy) {
    int ax = omv_esp32_edge_abs_i32(gx);
    int ay = omv_esp32_edge_abs_i32(gy);
    int major = IM_MAX(ax, ay);
    int minor = IM_MIN(ax, ay);
    return major + (minor >> 1);
}

static inline uint16_t omv_esp32_edge_grad_dir_fast(int gx, int gy) {
    int ax = omv_esp32_edge_abs_i32(gx);
    int ay = omv_esp32_edge_abs_i32(gy);

    if ((ay * 1000) < (ax * 414)) {
        return 0;
    }

    if ((ay * 1000) > (ax * 2414)) {
        return 90;
    }

    return ((gx < 0) ^ (gy < 0)) ? 135 : 45;
}

static bool omv_esp32_edge_validate(image_t *img, rectangle_t *roi) {
    return (img != NULL) && (roi != NULL) && (img->pixfmt == PIXFORMAT_RGB565) &&
           (img->pixels != NULL) && (img->w > 2) && (img->h > 2) &&
           (roi->w > 2) && (roi->h > 2);
}

static void omv_esp32_edge_normalize_roi(image_t *img, rectangle_t *roi) {
    roi->x = omv_esp32_edge_clamp_i32(roi->x, 0, img->w - 1);
    roi->y = omv_esp32_edge_clamp_i32(roi->y, 0, img->h - 1);
    roi->w = omv_esp32_edge_clamp_i32(roi->w, 1, img->w - roi->x);
    roi->h = omv_esp32_edge_clamp_i32(roi->h, 1, img->h - roi->y);
}

static bool omv_esp32_edge_ensure_grad_capacity(size_t count) {
    if (omv_esp32_edge_grad_buf_cap >= count) {
        return true;
    }

    omv_esp32_edge_grad_t *new_buf = realloc(omv_esp32_edge_grad_buf, count * sizeof(omv_esp32_edge_grad_t));
    if (new_buf == NULL) {
        return false;
    }

    omv_esp32_edge_grad_buf = new_buf;
    omv_esp32_edge_grad_buf_cap = count;
    return true;
}

bool omv_esp32_imlib_edge_simple(image_t *img, rectangle_t *roi, int low_thresh, int high_thresh) {
    (void) high_thresh;
    if (!omv_esp32_edge_validate(img, roi)) {
        return false;
    }

    omv_esp32_edge_normalize_roi(img, roi);

    uint8_t *gray = NULL;
    if (!omv_esp32_imlib_gray_get(img, (const uint8_t **) &gray)) {
        return false;
    }

    uint16_t *pixels = (uint16_t *) img->pixels;
    int x0 = roi->x;
    int x1 = roi->x + roi->w - 1;
    int y0 = roi->y;
    int y1 = roi->y + roi->h - 1;
    int w = img->w;

    // Fill top and bottom border rows
    for (int x = x0; x <= x1; x++) {
        pixels[y0 * w + x] = COLOR_RGB565_BINARY_MIN;
        pixels[y1 * w + x] = COLOR_RGB565_BINARY_MIN;
    }
    // Fill left and right border columns
    for (int y = y0 + 1; y < y1; y++) {
        pixels[y * w + x0] = COLOR_RGB565_BINARY_MIN;
        pixels[y * w + x1] = COLOR_RGB565_BINARY_MIN;
    }

    // Process interior pixels — no branch needed inside inner loop
    for (int y = y0 + 1; y < y1; y++) {
        const uint8_t *rp = gray + (y - 1) * w;
        const uint8_t *rc = gray + y * w;
        const uint8_t *rn = gray + (y + 1) * w;
        uint16_t *prow = pixels + y * w;

        for (int x = x0 + 1; x < x1; x++) {
            // Cache shared corner pixels used by both vx and vy
            int tl = rp[x - 1], tr = rp[x + 1];
            int bl = rn[x - 1], br = rn[x + 1];

            int vx = tl - tr + ((rc[x - 1] - rc[x + 1]) << 1) + bl - br;
            int vy = tl + (rp[x] << 1) + tr - bl - (rn[x] << 1) - br;

            int g = omv_esp32_edge_grad_mag_fast(vx, vy);
            prow[x] = (g >= low_thresh) ? COLOR_RGB565_BINARY_MAX : COLOR_RGB565_BINARY_MIN;
        }
    }
    return true;
}

bool omv_esp32_imlib_edge_canny(image_t *img, rectangle_t *roi, int low_thresh, int high_thresh) {
    if (!omv_esp32_edge_validate(img, roi)) {
        return false;
    }

    omv_esp32_edge_normalize_roi(img, roi);

    uint8_t *gray = NULL;
    if (!omv_esp32_imlib_gray_get(img, (const uint8_t **) &gray)) {
        return false;
    }

    size_t roi_count = (size_t) roi->w * (size_t) roi->h;
    if (!omv_esp32_edge_ensure_grad_capacity(roi_count)) {
        return false;
    }
    omv_esp32_edge_grad_t *gm = omv_esp32_edge_grad_buf;
    memset(gm, 0, roi_count * sizeof(omv_esp32_edge_grad_t));

    int x0 = roi->x;
    int x1 = roi->x + roi->w - 1;
    int y0 = roi->y;
    int y1 = roi->y + roi->h - 1;
    int w = img->w;
    int rw = roi->w;

    // Gradient computation pass — row pointers avoid per-pixel multiplications
    for (int gy = 1, y = y0 + 1; y < y1; y++, gy++) {
        const uint8_t *rp = gray + (y - 1) * w;
        const uint8_t *rc = gray + y * w;
        const uint8_t *rn = gray + (y + 1) * w;
        omv_esp32_edge_grad_t *grow = gm + gy * rw;

        for (int gx = 1, x = x0 + 1; x < x1; x++, gx++) {
            // Cache shared corner pixels used by both vx and vy
            int tl = rp[x - 1], tr = rp[x + 1];
            int bl = rn[x - 1], br = rn[x + 1];

            int vx = tl - tr + ((rc[x - 1] - rc[x + 1]) << 1) + bl - br;
            int vy = tl + (rp[x] << 1) + tr - bl - (rn[x] << 1) - br;

            grow[gx].g = omv_esp32_edge_grad_mag_fast(vx, vy);
            grow[gx].t = omv_esp32_edge_grad_dir_fast(vx, vy);
        }
    }

    // NMS + hysteresis pass
    uint16_t *pixels = (uint16_t *) img->pixels;
    for (int gy = 0, y = y0; y <= y1; y++, gy++) {
        uint16_t *prow = pixels + y * w;

        // Top/bottom border rows — fill and skip
        if ((y == y0) || (y == y1)) {
            for (int x = x0; x <= x1; x++) {
                prow[x] = COLOR_RGB565_BINARY_MIN;
            }
            continue;
        }

        // Precompute row pointers into grad buffer
        omv_esp32_edge_grad_t *grow      = gm + gy * rw;
        omv_esp32_edge_grad_t *grow_prev = grow - rw;
        omv_esp32_edge_grad_t *grow_next = grow + rw;

        // Left border column
        prow[x0] = COLOR_RGB565_BINARY_MIN;

        for (int gx = 1, x = x0 + 1; x < x1; x++, gx++) {
            omv_esp32_edge_grad_t *vc = &grow[gx];

            if (vc->g < low_thresh) {
                prow[x] = COLOR_RGB565_BINARY_MIN;
                continue;
            }

            // Hysteresis: reject weak edges with no strong neighbor
            if (!(vc->g              >= high_thresh ||
                  grow_prev[gx - 1].g >= high_thresh ||
                  grow_prev[gx    ].g >= high_thresh ||
                  grow_prev[gx + 1].g >= high_thresh ||
                  grow[gx - 1].g      >= high_thresh ||
                  grow[gx + 1].g      >= high_thresh ||
                  grow_next[gx - 1].g >= high_thresh ||
                  grow_next[gx    ].g >= high_thresh ||
                  grow_next[gx + 1].g >= high_thresh)) {
                prow[x] = COLOR_RGB565_BINARY_MIN;
                continue;
            }

            // Non-maximum suppression along gradient direction
            omv_esp32_edge_grad_t *va;
            omv_esp32_edge_grad_t *vb;
            switch (vc->t) {
                case 0:
                    va = &grow[gx - 1];
                    vb = &grow[gx + 1];
                    break;
                case 45:
                    va = &grow_next[gx - 1];
                    vb = &grow_prev[gx + 1];
                    break;
                case 90:
                    va = &grow_next[gx];
                    vb = &grow_prev[gx];
                    break;
                default:
                    va = &grow_next[gx + 1];
                    vb = &grow_prev[gx - 1];
                    break;
            }

            prow[x] = ((vc->g > va->g) && (vc->g > vb->g))
                          ? COLOR_RGB565_BINARY_MAX
                          : COLOR_RGB565_BINARY_MIN;
        }

        // Right border column
        prow[x1] = COLOR_RGB565_BINARY_MIN;
    }

    omv_esp32_imlib_gray_mark_dirty();
    return true;
}
