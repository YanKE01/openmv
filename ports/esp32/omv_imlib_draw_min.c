/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal OpenMV imlib draw subset for ESP32 bring-up.
 * Extracted from OpenMV's lib/imlib/imlib.c and lib/imlib/draw.c
 * to avoid pulling the full imlib dependency graph in one step.
 */

#include <stdlib.h>

#include "imlib.h"

int imlib_get_pixel(image_t *img, int x, int y) {
    if (!((0 <= x) && (x < img->w) && (0 <= y) && (y < img->h))) {
        return -1;
    }

    switch (img->pixfmt) {
        case PIXFORMAT_BINARY:
            return IMAGE_GET_BINARY_PIXEL(img, x, y);
        case PIXFORMAT_GRAYSCALE:
            return IMAGE_GET_GRAYSCALE_PIXEL(img, x, y);
        case PIXFORMAT_RGB565:
            return IMAGE_GET_RGB565_PIXEL(img, x, y);
        default:
            return -1;
    }
}

void imlib_set_pixel(image_t *img, int x, int y, int p) {
    if (!((0 <= x) && (x < img->w) && (0 <= y) && (y < img->h))) {
        return;
    }

    switch (img->pixfmt) {
        case PIXFORMAT_BINARY:
            IMAGE_PUT_BINARY_PIXEL(img, x, y, p);
            break;
        case PIXFORMAT_GRAYSCALE:
            IMAGE_PUT_GRAYSCALE_PIXEL(img, x, y, p);
            break;
        case PIXFORMAT_RGB565:
            IMAGE_PUT_RGB565_PIXEL(img, x, y, p);
            break;
        default:
            break;
    }
}

bool lb_clip_line(line_t *l, int x, int y, int w, int h) {
    int xdelta = l->x2 - l->x1;
    int ydelta = l->y2 - l->y1;
    int p[4];
    int q[4];
    float umin = 0.0f;
    float umax = 1.0f;

    p[0] = -xdelta;
    p[1] = xdelta;
    p[2] = -ydelta;
    p[3] = ydelta;

    q[0] = l->x1 - x;
    q[1] = (x + w - 1) - l->x1;
    q[2] = l->y1 - y;
    q[3] = (y + h - 1) - l->y1;

    for (int i = 0; i < 4; i++) {
        if (p[i]) {
            float u = ((float) q[i]) / ((float) p[i]);

            if (p[i] < 0) {
                if (u > umax) {
                    return false;
                }
                if (u > umin) {
                    umin = u;
                }
            }

            if (p[i] > 0) {
                if (u < umin) {
                    return false;
                }
                if (u < umax) {
                    umax = u;
                }
            }
        } else if (q[i] < 0) {
            return false;
        }
    }

    if (umax < umin) {
        return false;
    }

    l->x1 = l->x1 + (xdelta * umin);
    l->y1 = l->y1 + (ydelta * umin);
    l->x2 = l->x1 + (xdelta * (umax - umin));
    l->y2 = l->y1 + (ydelta * (umax - umin));
    return true;
}

static void imlib_set_pixel_aa(image_t *img, int x, int y, int err, int c) {
    if (!((0 <= x) && (x < img->w) && (0 <= y) && (y < img->h))) {
        return;
    }

    switch (img->pixfmt) {
        case PIXFORMAT_BINARY: {
            uint32_t *ptr = IMAGE_COMPUTE_BINARY_PIXEL_ROW_PTR(img, y);
            int old_c = IMAGE_GET_BINARY_PIXEL_FAST(ptr, x) * 255;
            int new_c = (((old_c * err) + ((c ? 255 : 0) * (256 - err))) >> 8) > 127;
            IMAGE_PUT_BINARY_PIXEL_FAST(ptr, x, new_c);
            break;
        }
        case PIXFORMAT_GRAYSCALE: {
            uint8_t *ptr = IMAGE_COMPUTE_GRAYSCALE_PIXEL_ROW_PTR(img, y);
            int old_c = IMAGE_GET_GRAYSCALE_PIXEL_FAST(ptr, x);
            int new_c = ((old_c * err) + ((c & 0xff) * (256 - err))) >> 8;
            IMAGE_PUT_GRAYSCALE_PIXEL_FAST(ptr, x, new_c);
            break;
        }
        case PIXFORMAT_RGB565: {
            uint16_t *ptr = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(img, y);
            int old_c = IMAGE_GET_RGB565_PIXEL_FAST(ptr, x);
            int old_c_r5 = COLOR_RGB565_TO_R5(old_c);
            int old_c_g6 = COLOR_RGB565_TO_G6(old_c);
            int old_c_b5 = COLOR_RGB565_TO_B5(old_c);
            int c_r5 = COLOR_RGB565_TO_R5(c);
            int c_g6 = COLOR_RGB565_TO_G6(c);
            int c_b5 = COLOR_RGB565_TO_B5(c);
            int new_c_r5 = ((old_c_r5 * err) + (c_r5 * (256 - err))) >> 8;
            int new_c_g6 = ((old_c_g6 * err) + (c_g6 * (256 - err))) >> 8;
            int new_c_b5 = ((old_c_b5 * err) + (c_b5 * (256 - err))) >> 8;
            IMAGE_PUT_RGB565_PIXEL_FAST(ptr, x, COLOR_R5_G6_B5_TO_RGB565(new_c_r5, new_c_g6, new_c_b5));
            break;
        }
        default:
            break;
    }
}

static void imlib_draw_thin_line(image_t *img, int x0, int y0, int x1, int y1, int c) {
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int e2;
    int x2;
    int ed = dx + dy == 0 ? 1 : fast_floorf(fast_sqrtf(dx * dx + dy * dy));

    for (;;) {
        imlib_set_pixel_aa(img, x0, y0, 256 * abs(err - dx + dy) / ed, c);
        e2 = err;
        x2 = x0;
        if (2 * e2 >= -dx) {
            if (x0 == x1) {
                break;
            }
            if (e2 + dy < ed) {
                imlib_set_pixel_aa(img, x0, y0 + sy, 256 * (e2 + dy) / ed, c);
            }
            err -= dy;
            x0 += sx;
        }
        if (2 * e2 <= dy) {
            if (y0 == y1) {
                break;
            }
            if (dx - e2 < ed) {
                imlib_set_pixel_aa(img, x2 + sx, y0, 256 * (dx - e2) / ed, c);
            }
            err += dx;
            y0 += sy;
        }
    }
}

void imlib_draw_line(image_t *img, int x0, int y0, int x1, int y1, int c, int th) {
    line_t line = { x0, y0, x1, y1 };
    if (!lb_clip_line(&line, 0, 0, img->w, img->h)) {
        return;
    }

    x0 = line.x1;
    y0 = line.y1;
    x1 = line.x2;
    y1 = line.y2;

    const int ex = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int ey = abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int e2 = fast_floorf(fast_sqrtf(ex * ex + ey * ey));

    if (th <= 1 || e2 == 0) {
        imlib_draw_thin_line(img, x0, y0, x1, y1, c);
        return;
    }

    int dx = ex * 256 / e2;
    int dy = ey * 256 / e2;
    th = 256 * (th - 1);

    if (dx < dy) {
        x1 = (e2 + th / 2) / dy;
        int err = x1 * dy - th / 2;
        err = IM_MAX(err, 0);
        for (x0 -= x1 * sx;; y0 += sy) {
            x1 = x0;
            imlib_set_pixel_aa(img, x1, y0, err, c);
            for (e2 = dy - err - th; e2 + dy < 256; e2 += dy) {
                x1 += sx;
                imlib_set_pixel(img, x1, y0, c);
            }
            imlib_set_pixel_aa(img, x1 + sx, y0, e2, c);
            if (y0 == y1) {
                break;
            }
            err += dx;
            if (err > 256) {
                err -= dy;
                x0 += sx;
            }
        }
    } else {
        y1 = (e2 + th / 2) / dx;
        int err = y1 * dx - th / 2;
        err = IM_MAX(err, 0);
        for (y0 -= y1 * sy;; x0 += sx) {
            y1 = y0;
            imlib_set_pixel_aa(img, x0, y1, err, c);
            for (e2 = dx - err - th; e2 + dx < 256; e2 += dx) {
                y1 += sy;
                imlib_set_pixel(img, x0, y1, c);
            }
            imlib_set_pixel_aa(img, x0, y1 + sy, e2, c);
            if (x0 == x1) {
                break;
            }
            err += dy;
            if (err > 256) {
                err -= dx;
                y0 += sy;
            }
        }
    }
}

static void imlib_xline(image_t *img, int x1, int x2, int y, int c) {
    while (x1 <= x2) {
        imlib_set_pixel(img, x1++, y, c);
    }
}

static void imlib_yline(image_t *img, int x, int y1, int y2, int c) {
    while (y1 <= y2) {
        imlib_set_pixel(img, x, y1++, c);
    }
}

static void imlib_point_fill(image_t *img, int cx, int cy, int r0, int r1, int c) {
    for (int y = r0; y <= r1; y++) {
        for (int x = r0; x <= r1; x++) {
            if (((x * x) + (y * y)) <= (r0 * r0)) {
                imlib_set_pixel(img, cx + x, cy + y, c);
            }
        }
    }
}

void imlib_draw_rectangle(image_t *img, int rx, int ry, int rw, int rh, int c, int thickness, bool fill) {
    if (fill) {
        for (int y = ry, yy = ry + rh; y < yy; y++) {
            for (int x = rx, xx = rx + rw; x < xx; x++) {
                imlib_set_pixel(img, x, y, c);
            }
        }
    } else if (thickness > 0) {
        int thickness0 = (thickness - 0) / 2;
        int thickness1 = (thickness - 1) / 2;

        for (int i = rx - thickness0, j = rx + rw + thickness1, k = ry + rh - 1; i < j; i++) {
            imlib_yline(img, i, ry - thickness0, ry + thickness1, c);
            imlib_yline(img, i, k - thickness0, k + thickness1, c);
        }

        for (int i = ry - thickness0, j = ry + rh + thickness1, k = rx + rw - 1; i < j; i++) {
            imlib_xline(img, rx - thickness0, rx + thickness1, i, c);
            imlib_xline(img, k - thickness0, k + thickness1, i, c);
        }
    }
}

static void imlib_draw_circle_thin(image_t *img, int cx, int cy, int r, int c, bool fill) {
    int x = r;
    int y = 0;
    int err = 2 - (2 * r);
    r = 1 - err;
    for (;;) {
        int i = 256 * abs(err + (2 * (x + y)) - 2) / r;
        imlib_set_pixel_aa(img, cx + x, cy - y, i, c);
        imlib_set_pixel_aa(img, cx + y, cy + x, i, c);
        imlib_set_pixel_aa(img, cx - x, cy + y, i, c);
        imlib_set_pixel_aa(img, cx - y, cy - x, i, c);
        if (fill) {
            imlib_xline(img, cx, cx + x - 1, cy - y, c);
            imlib_yline(img, cx + y, cy, cy + x - 1, c);
            imlib_xline(img, cx - x + 1, cx, cy + y, c);
            imlib_yline(img, cx - y, cy - x + 1, cy, c);
        }
        if (x == 0) {
            break;
        }
        int e2 = err;
        int x2 = x;
        if (err > y) {
            i = 256 * (err + (2 * x) - 1) / r;
            if (i < 256) {
                imlib_set_pixel_aa(img, cx + x, cy - y + 1, i, c);
                imlib_set_pixel_aa(img, cx + y - 1, cy + x, i, c);
                imlib_set_pixel_aa(img, cx - x, cy + y - 1, i, c);
                imlib_set_pixel_aa(img, cx - y + 1, cy - x, i, c);
            }
            err -= (--x * 2) - 1;
        }
        if (e2 <= x2--) {
            if (!fill) {
                i = 256 * (1 - (2 * y) - e2) / r;
                if (i < 256) {
                    imlib_set_pixel_aa(img, cx + x2, cy - y, i, c);
                    imlib_set_pixel_aa(img, cx + y, cy + x2, i, c);
                    imlib_set_pixel_aa(img, cx - x2, cy + y, i, c);
                    imlib_set_pixel_aa(img, cx - y, cy - x2, i, c);
                }
            }
            err -= (--y * 2) - 1;
        }
    }
}

void imlib_draw_circle(image_t *img, int cx, int cy, int r, int c, int thickness, bool fill) {
    if ((r == 0) && (fill || (thickness > 0))) {
        imlib_set_pixel(img, cx, cy, c);
    }

    if ((r <= 0) || ((!fill) && (thickness <= 0))) {
        return;
    }

    if (thickness == 1 || fill) {
        imlib_draw_circle_thin(img, cx, cy, r + (IM_MAX(thickness, 0) / 2), c, fill);
    } else {
        int thickness0 = (thickness - 0) / 2;
        int thickness1 = (thickness - 1) / 2;

        int xo = r + thickness0;
        int xi = IM_MAX(r - thickness1, 0);
        int xi_tmp = xi;
        int y = 0;
        int erro = 1 - xo;
        int erri = 1 - xi;

        while (xo >= y) {
            imlib_xline(img, cx + xi, cx + xo, cy + y, c);
            imlib_yline(img, cx + y, cy + xi, cy + xo, c);
            imlib_xline(img, cx - xo, cx - xi, cy + y, c);
            imlib_yline(img, cx - y, cy + xi, cy + xo, c);
            imlib_xline(img, cx - xo, cx - xi, cy - y, c);
            imlib_yline(img, cx - y, cy - xo, cy - xi, c);
            imlib_xline(img, cx + xi, cx + xo, cy - y, c);
            imlib_yline(img, cx + y, cy - xo, cy - xi, c);

            y++;

            if (erro < 0) {
                erro += 2 * y + 1;
            } else {
                xo--;
                erro += 2 * (y - xo + 1);
            }

            if (y > xi_tmp) {
                xi = y;
            } else if (erri < 0) {
                erri += 2 * y + 1;
            } else {
                xi--;
                erri += 2 * (y - xi + 1);
            }
        }

        imlib_draw_circle_thin(img, cx, cy, r + thickness0, c, false);
        imlib_draw_circle_thin(img, cx, cy, xi_tmp, c, false);
    }
}

void imlib_draw_image(image_t *dst_img,
                      image_t *src_img,
                      int dst_x_start,
                      int dst_y_start,
                      float x_scale,
                      float y_scale,
                      rectangle_t *roi,
                      int rgb_channel,
                      int alpha,
                      const uint16_t *color_palette,
                      const uint8_t *alpha_palette,
                      image_hint_t hint,
                      float *transform,
                      imlib_draw_row_callback_t callback,
                      void *callback_arg,
                      void *dst_row_override) {
    (void) rgb_channel;
    (void) alpha;
    (void) color_palette;
    (void) alpha_palette;
    (void) hint;
    (void) transform;
    (void) callback;
    (void) callback_arg;
    (void) dst_row_override;

    if ((x_scale != 1.0f) || (y_scale != 1.0f)) {
        return;
    }

    rectangle_t full_roi = {
        .x = 0,
        .y = 0,
        .w = src_img->w,
        .h = src_img->h,
    };
    if (roi == NULL) {
        roi = &full_roi;
    }

    for (int y = 0; y < roi->h; y++) {
        for (int x = 0; x < roi->w; x++) {
            int src_x = roi->x + x;
            int src_y = roi->y + y;
            int dst_x = dst_x_start + x;
            int dst_y = dst_y_start + y;

            if ((src_x < 0) || (src_x >= src_img->w) || (src_y < 0) || (src_y >= src_img->h) ||
                (dst_x < 0) || (dst_x >= dst_img->w) || (dst_y < 0) || (dst_y >= dst_img->h)) {
                continue;
            }

            if ((dst_img->pixfmt == PIXFORMAT_GRAYSCALE) && (src_img->pixfmt == PIXFORMAT_RGB565)) {
                uint16_t pixel = IMAGE_GET_RGB565_PIXEL(src_img, src_x, src_y);
                IMAGE_PUT_GRAYSCALE_PIXEL(dst_img, dst_x, dst_y, COLOR_RGB565_TO_Y(pixel));
            } else if ((dst_img->pixfmt == PIXFORMAT_GRAYSCALE) && (src_img->pixfmt == PIXFORMAT_GRAYSCALE)) {
                IMAGE_PUT_GRAYSCALE_PIXEL(dst_img, dst_x, dst_y,
                                          IMAGE_GET_GRAYSCALE_PIXEL(src_img, src_x, src_y));
            } else if ((dst_img->pixfmt == PIXFORMAT_RGB565) && (src_img->pixfmt == PIXFORMAT_RGB565)) {
                IMAGE_PUT_RGB565_PIXEL(dst_img, dst_x, dst_y,
                                       IMAGE_GET_RGB565_PIXEL(src_img, src_x, src_y));
            }
        }
    }
}

void imlib_invert(image_t *img) {
    uint32_t n = img->size;
    uint32_t *p32 = (uint32_t *) img->data;

    switch (img->pixfmt) {
        case PIXFORMAT_BINARY:
        case PIXFORMAT_GRAYSCALE: {
            for (; n >= 4; n -= 4, p32++) {
                *p32 = ~(*p32);
            }

            uint8_t *p8 = (uint8_t *) p32;
            for (; n >= 1; n -= 1, p8++) {
                *p8 = ~(*p8);
            }
            break;
        }
        case PIXFORMAT_RGB565: {
            for (; n >= 4; n -= 4, p32++) {
                *p32 = ~(*p32);
            }

            uint16_t *p16 = (uint16_t *) p32;
            for (; n >= 2; n -= 2, p16++) {
                *p16 = ~(*p16);
            }
            break;
        }
        default:
            break;
    }
}
