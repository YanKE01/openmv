/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal OpenMV imlib draw subset for ESP32 bring-up.
 */

#include <stdlib.h>

#include "font.h"
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

static void omv_point_rotate_min(int x, int y, float r, int center_x, int center_y, int16_t *new_x, int16_t *new_y) {
    float s = sinf(r);
    float c = cosf(r);
    x -= center_x;
    y -= center_y;
    *new_x = fast_roundf((x * c) - (y * s)) + center_x;
    *new_y = fast_roundf((x * s) + (y * c)) + center_y;
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

void imlib_draw_string(image_t *img,
                       int x_off,
                       int y_off,
                       const char *str,
                       int c,
                       float scale,
                       int x_spacing,
                       int y_spacing,
                       bool mono_space,
                       int char_rotation,
                       bool char_hmirror,
                       bool char_vflip,
                       int string_rotation,
                       bool string_hmirror,
                       bool string_vflip) {
    char_rotation %= 360;
    if (char_rotation < 0) {
        char_rotation += 360;
    }
    char_rotation = (char_rotation / 90) * 90;

    string_rotation %= 360;
    if (string_rotation < 0) {
        string_rotation += 360;
    }
    string_rotation = (string_rotation / 90) * 90;

    bool char_swap_w_h = (char_rotation == 90) || (char_rotation == 270);
    bool char_upsidedown = (char_rotation == 180) || (char_rotation == 270);

    if (string_hmirror) {
        x_off -= fast_floorf(font[0].w * scale) - 1;
    }
    if (string_vflip) {
        y_off -= fast_floorf(font[0].h * scale) - 1;
    }

    int org_x_off = x_off;
    int org_y_off = y_off;
    const int anchor = x_off;

    for (char ch, last = '\0'; (ch = *str); str++, last = ch) {
        if ((last == '\r') && (ch == '\n')) {
            continue;
        }

        if ((ch == '\n') || (ch == '\r')) {
            x_off = anchor;
            y_off += (string_vflip ? -1 : +1) *
                     (fast_floorf((char_swap_w_h ? font[0].w : font[0].h) * scale) + y_spacing);
            continue;
        }

        if ((ch < ' ') || (ch > '~')) {
            continue;
        }

        const glyph_t *g = &font[ch - ' '];

        if (!mono_space) {
            bool exit = false;

            if (!char_swap_w_h) {
                for (int x = 0, xx = g->w; x < xx; x++) {
                    for (int y = 0, yy = g->h; y < yy; y++) {
                        if (g->data[(char_upsidedown ^ char_vflip) ? (g->h - 1 - y) : y] &
                            (1 << ((char_upsidedown ^ char_hmirror ^ string_hmirror) ? x : (g->w - 1 - x)))) {
                            x_off += (string_hmirror ? +1 : -1) * fast_floorf(x * scale);
                            exit = true;
                            break;
                        }
                    }

                    if (exit) {
                        break;
                    }
                }
            } else {
                for (int y = g->h - 1; y >= 0; y--) {
                    for (int x = 0, xx = g->w; x < xx; x++) {
                        if (g->data[(char_upsidedown ^ char_vflip) ? (g->h - 1 - y) : y] &
                            (1 << ((char_upsidedown ^ char_hmirror ^ string_hmirror) ? x : (g->w - 1 - x)))) {
                            x_off += (string_hmirror ? +1 : -1) * fast_floorf((g->h - 1 - y) * scale);
                            exit = true;
                            break;
                        }
                    }

                    if (exit) {
                        break;
                    }
                }
            }
        }

        for (int y = 0, yy = fast_floorf(g->h * scale); y < yy; y++) {
            for (int x = 0, xx = fast_floorf(g->w * scale); x < xx; x++) {
                if (g->data[fast_floorf(y / scale)] & (1 << (g->w - 1 - fast_floorf(x / scale)))) {
                    int16_t x_tmp = x_off + (char_hmirror ? (xx - x - 1) : x);
                    int16_t y_tmp = y_off + (char_vflip ? (yy - y - 1) : y);
                    omv_point_rotate_min(x_tmp, y_tmp, IM_DEG2RAD(char_rotation), x_off + (xx / 2), y_off + (yy / 2), &x_tmp, &y_tmp);
                    omv_point_rotate_min(x_tmp, y_tmp, IM_DEG2RAD(string_rotation), org_x_off, org_y_off, &x_tmp, &y_tmp);
                    imlib_set_pixel(img, x_tmp, y_tmp, c);
                }
            }
        }

        if (mono_space) {
            x_off += (string_hmirror ? -1 : +1) *
                     (fast_floorf((char_swap_w_h ? g->h : g->w) * scale) + x_spacing);
        } else {
            bool exit = false;

            if (!char_swap_w_h) {
                for (int x = g->w - 1; x >= 0; x--) {
                    for (int y = g->h - 1; y >= 0; y--) {
                        if (g->data[(char_upsidedown ^ char_vflip) ? (g->h - 1 - y) : y] &
                            (1 << ((char_upsidedown ^ char_hmirror ^ string_hmirror) ? x : (g->w - 1 - x)))) {
                            x_off += (string_hmirror ? -1 : +1) * (fast_floorf((x + 2) * scale) + x_spacing);
                            exit = true;
                            break;
                        }
                    }

                    if (exit) {
                        break;
                    }
                }
            } else {
                for (int y = 0, yy = g->h; y < yy; y++) {
                    for (int x = g->w - 1; x >= 0; x--) {
                        if (g->data[(char_upsidedown ^ char_vflip) ? (g->h - 1 - y) : y] &
                            (1 << ((char_upsidedown ^ char_hmirror ^ string_hmirror) ? x : (g->w - 1 - x)))) {
                            x_off += (string_hmirror ? -1 : +1) *
                                     (fast_floorf(((g->h - 1 - y) + 2) * scale) + x_spacing);
                            exit = true;
                            break;
                        }
                    }

                    if (exit) {
                        break;
                    }
                }
            }

            if (!exit) {
                x_off += (string_hmirror ? -1 : +1) * fast_floorf(scale * 3);
            }
        }
    }
}
