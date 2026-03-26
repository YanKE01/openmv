/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal geometry helpers needed by selected imlib features on ESP32.
 */

#include "imlib.h"

void rectangle_init(rectangle_t *ptr, int x, int y, int w, int h) {
    ptr->x = x;
    ptr->y = y;
    ptr->w = w;
    ptr->h = h;
}

void rectangle_united(rectangle_t *dst, rectangle_t *src) {
    int left_x = IM_MIN(dst->x, src->x);
    int top_y = IM_MIN(dst->y, src->y);
    int right_x = IM_MAX(dst->x + dst->w, src->x + src->w);
    int bottom_y = IM_MAX(dst->y + dst->h, src->y + src->h);

    dst->x = left_x;
    dst->y = top_y;
    dst->w = right_x - left_x;
    dst->h = bottom_y - top_y;
}

bool rectangle_overlap(rectangle_t *ptr0, rectangle_t *ptr1) {
    int left = IM_MAX(ptr0->x, ptr1->x);
    int top = IM_MAX(ptr0->y, ptr1->y);
    int right = IM_MIN(ptr0->x + ptr0->w, ptr1->x + ptr1->w);
    int bottom = IM_MIN(ptr0->y + ptr0->h, ptr1->y + ptr1->h);
    return (left < right) && (top < bottom);
}
