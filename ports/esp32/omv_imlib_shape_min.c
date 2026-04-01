/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal rect/circle detection subset for ESP32 bring-up.
 * This is intentionally smaller and less exact than the full OpenMV Hough path.
 */

#include <string.h>

#include "esp_heap_caps.h"

#include "omv_imlib_gray_min.h"
#include "omv_imlib_shape_min.h"

typedef struct {
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    uint32_t pixels;
    uint32_t score;
    uint32_t dist_acc;
    bool used;
} omv_esp32_shape_accum_t;

static void *omv_esp32_shape_alloc_cleared(size_t size) {
    return heap_caps_calloc_prefer(1, size, 2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void omv_esp32_shape_free(void *ptr) {
    heap_caps_free(ptr);
}

static inline int omv_esp32_abs_i(int v) {
    return (v < 0) ? -v : v;
}

static uint16_t omv_esp32_shape_find_root(uint16_t *parents, uint16_t label) {
    while (parents[label] != label) {
        parents[label] = parents[parents[label]];
        label = parents[label];
    }
    return label;
}

static void omv_esp32_shape_union(uint16_t *parents, uint16_t a, uint16_t b) {
    uint16_t ra = omv_esp32_shape_find_root(parents, a);
    uint16_t rb = omv_esp32_shape_find_root(parents, b);
    if (ra == rb) {
        return;
    }
    if (ra < rb) {
        parents[rb] = ra;
    } else {
        parents[ra] = rb;
    }
}

static bool omv_esp32_shape_build_edge_labels(image_t *img,
                                              rectangle_t *roi,
                                              unsigned int x_stride,
                                              unsigned int y_stride,
                                              int edge_threshold,
                                              uint16_t **out_labels,
                                              uint16_t **out_parents,
                                              size_t *out_w,
                                              size_t *out_h,
                                              uint16_t *out_next_label) {
    const uint8_t *gray = NULL;
    if (!omv_esp32_imlib_gray_get(img, &gray)) {
        return false;
    }

    int roi_x0 = IM_MAX(roi->x, 1);
    int roi_y0 = IM_MAX(roi->y, 1);
    int roi_x1 = IM_MIN(roi->x + roi->w, img->w - 1);
    int roi_y1 = IM_MIN(roi->y + roi->h, img->h - 1);

    if ((roi_x1 - roi_x0) < 3 || (roi_y1 - roi_y0) < 3) {
        *out_labels = NULL;
        *out_parents = NULL;
        *out_w = 0;
        *out_h = 0;
        *out_next_label = 1;
        return true;
    }

    size_t roi_w = (size_t) (roi_x1 - roi_x0);
    size_t roi_h = (size_t) (roi_y1 - roi_y0);
    size_t roi_pixels = roi_w * roi_h;

    uint16_t *labels = omv_esp32_shape_alloc_cleared(roi_pixels * sizeof(uint16_t));
    uint16_t *parents = omv_esp32_shape_alloc_cleared((roi_pixels + 1) * sizeof(uint16_t));
    if ((labels == NULL) || (parents == NULL)) {
        omv_esp32_shape_free(labels);
        omv_esp32_shape_free(parents);
        return false;
    }

    uint16_t next_label = 1;
    for (int y = roi_y0; y < roi_y1; y += (int) y_stride) {
        for (int x = roi_x0; x < roi_x1; x += (int) x_stride) {
            int gx = omv_esp32_abs_i(gray[(y * img->w) + (x + 1)] -
                                     gray[(y * img->w) + (x - 1)]);
            int gy = omv_esp32_abs_i(gray[((y + 1) * img->w) + x] -
                                     gray[((y - 1) * img->w) + x]);
            int mag = gx + gy;
            if (mag < edge_threshold) {
                continue;
            }

            size_t idx = (size_t) (y - roi_y0) * roi_w + (size_t) (x - roi_x0);
            uint16_t neighbors[4] = {0};
            size_t neighbor_count = 0;

            if (x > roi_x0) {
                uint16_t left = labels[idx - x_stride];
                if (left != 0) {
                    neighbors[neighbor_count++] = left;
                }
            }
            if (y > roi_y0) {
                size_t up_idx = idx - (roi_w * y_stride);
                uint16_t up = labels[up_idx];
                if (up != 0) {
                    neighbors[neighbor_count++] = up;
                }
                if (x > roi_x0) {
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
                    omv_esp32_shape_free(labels);
                    omv_esp32_shape_free(parents);
                    return false;
                }
                labels[idx] = next_label;
                parents[next_label] = next_label;
                next_label++;
            } else {
                uint16_t min_label = neighbors[0];
                for (size_t i = 1; i < neighbor_count; i++) {
                    if (neighbors[i] < min_label) {
                        min_label = neighbors[i];
                    }
                }
                labels[idx] = min_label;
                for (size_t i = 0; i < neighbor_count; i++) {
                    omv_esp32_shape_union(parents, min_label, neighbors[i]);
                }
            }
        }
    }

    *out_labels = labels;
    *out_parents = parents;
    *out_w = roi_w;
    *out_h = roi_h;
    *out_next_label = next_label;
    return true;
}

static bool omv_esp32_shape_accumulate(image_t *img,
                                       rectangle_t *roi,
                                       unsigned int x_stride,
                                       unsigned int y_stride,
                                       int edge_threshold,
                                       uint16_t **out_labels,
                                       uint16_t **out_parents,
                                       omv_esp32_shape_accum_t **out_accum,
                                       size_t *out_w,
                                       size_t *out_h,
                                       uint16_t *out_label_count) {
    const uint8_t *gray = NULL;
    if (!omv_esp32_imlib_gray_get(img, &gray)) {
        return false;
    }

    uint16_t *labels = NULL;
    uint16_t *parents = NULL;
    size_t roi_w = 0;
    size_t roi_h = 0;
    uint16_t next_label = 1;

    if (!omv_esp32_shape_build_edge_labels(img, roi, x_stride, y_stride, edge_threshold,
                                           &labels, &parents, &roi_w, &roi_h, &next_label)) {
        return false;
    }

    omv_esp32_shape_accum_t *accum = omv_esp32_shape_alloc_cleared(next_label * sizeof(omv_esp32_shape_accum_t));
    if (accum == NULL) {
        omv_esp32_shape_free(labels);
        omv_esp32_shape_free(parents);
        return false;
    }

    int roi_x0 = IM_MAX(roi->x, 1);
    int roi_y0 = IM_MAX(roi->y, 1);
    int roi_x1 = IM_MIN(roi->x + roi->w, img->w - 1);
    int roi_y1 = IM_MIN(roi->y + roi->h, img->h - 1);

    for (int y = roi_y0; y < roi_y1; y += (int) y_stride) {
        for (int x = roi_x0; x < roi_x1; x += (int) x_stride) {
            size_t idx = (size_t) (y - roi_y0) * roi_w + (size_t) (x - roi_x0);
            uint16_t label = labels[idx];
            if (label == 0) {
                continue;
            }

            int gx = omv_esp32_abs_i(gray[(y * img->w) + (x + 1)] -
                                     gray[(y * img->w) + (x - 1)]);
            int gy = omv_esp32_abs_i(gray[((y + 1) * img->w) + x] -
                                     gray[((y - 1) * img->w) + x]);
            int mag = gx + gy;
            if (mag < edge_threshold) {
                continue;
            }

            uint16_t root = omv_esp32_shape_find_root(parents, label);
            labels[idx] = root;
            omv_esp32_shape_accum_t *a = &accum[root];

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

            a->pixels++;
            a->score += (uint32_t) mag;
        }
    }

    *out_labels = labels;
    *out_parents = parents;
    *out_accum = accum;
    *out_w = roi_w;
    *out_h = roi_h;
    *out_label_count = next_label;
    return true;
}

bool omv_esp32_find_rects(image_t *img,
                          rectangle_t *roi,
                          uint32_t threshold,
                          find_rects_list_lnk_data_t **out_rects,
                          size_t *out_len) {
    if ((img == NULL) || (roi == NULL) || (out_rects == NULL) || (out_len == NULL) ||
        (img->pixfmt != PIXFORMAT_RGB565)) {
        return false;
    }

    *out_rects = NULL;
    *out_len = 0;

    uint16_t *labels = NULL;
    uint16_t *parents = NULL;
    omv_esp32_shape_accum_t *accum = NULL;
    size_t roi_w = 0;
    size_t roi_h = 0;
    uint16_t label_count = 0;

    if (!omv_esp32_shape_accumulate(img, roi, 1, 1, 32,
                                    &labels, &parents, &accum, &roi_w, &roi_h, &label_count)) {
        return false;
    }

    size_t valid_count = 0;
    for (size_t i = 1; i < label_count; i++) {
        if (!accum[i].used) {
            continue;
        }
        int w = accum[i].x1 - accum[i].x0 + 1;
        int h = accum[i].y1 - accum[i].y0 + 1;
        if ((w < 12) || (h < 12)) {
            continue;
        }
        uint32_t perimeter = (uint32_t) ((2 * w) + (2 * h));
        uint32_t magnitude = accum[i].pixels * 64;
        if ((perimeter < 40) || (magnitude < threshold)) {
            continue;
        }
        valid_count++;
    }

    if (valid_count != 0) {
        find_rects_list_lnk_data_t *rects = omv_esp32_shape_alloc_cleared(valid_count * sizeof(find_rects_list_lnk_data_t));
        if (rects == NULL) {
            omv_esp32_shape_free(accum);
            omv_esp32_shape_free(parents);
            omv_esp32_shape_free(labels);
            return false;
        }

        size_t out_index = 0;
        for (size_t i = 1; i < label_count; i++) {
            if (!accum[i].used) {
                continue;
            }
            int w = accum[i].x1 - accum[i].x0 + 1;
            int h = accum[i].y1 - accum[i].y0 + 1;
            uint32_t magnitude = accum[i].pixels * 64;
            if ((w < 12) || (h < 12) || (magnitude < threshold)) {
                continue;
            }

            rects[out_index].corners[0].x = accum[i].x0;
            rects[out_index].corners[0].y = accum[i].y0;
            rects[out_index].corners[1].x = accum[i].x1;
            rects[out_index].corners[1].y = accum[i].y0;
            rects[out_index].corners[2].x = accum[i].x1;
            rects[out_index].corners[2].y = accum[i].y1;
            rects[out_index].corners[3].x = accum[i].x0;
            rects[out_index].corners[3].y = accum[i].y1;
            rects[out_index].rect.x = accum[i].x0;
            rects[out_index].rect.y = accum[i].y0;
            rects[out_index].rect.w = w;
            rects[out_index].rect.h = h;
            rects[out_index].magnitude = magnitude;
            out_index++;
        }

        *out_rects = rects;
        *out_len = valid_count;
    }

    omv_esp32_shape_free(accum);
    omv_esp32_shape_free(parents);
    omv_esp32_shape_free(labels);
    return true;
}

void omv_esp32_free_rects(find_rects_list_lnk_data_t *rects) {
    omv_esp32_shape_free(rects);
}

static bool omv_esp32_circle_close(const find_circles_list_lnk_data_t *a,
                                   const find_circles_list_lnk_data_t *b,
                                   unsigned int x_margin,
                                   unsigned int y_margin,
                                   unsigned int r_margin) {
    return (omv_esp32_abs_i(a->p.x - b->p.x) <= (int) x_margin) &&
           (omv_esp32_abs_i(a->p.y - b->p.y) <= (int) y_margin) &&
           (omv_esp32_abs_i((int) a->r - (int) b->r) <= (int) r_margin);
}

bool omv_esp32_find_circles(image_t *img,
                            rectangle_t *roi,
                            unsigned int x_stride,
                            unsigned int y_stride,
                            uint32_t threshold,
                            unsigned int x_margin,
                            unsigned int y_margin,
                            unsigned int r_margin,
                            unsigned int r_min,
                            unsigned int r_max,
                            unsigned int r_step,
                            find_circles_list_lnk_data_t **out_circles,
                            size_t *out_len) {
    if ((img == NULL) || (roi == NULL) || (out_circles == NULL) || (out_len == NULL) ||
        (img->pixfmt != PIXFORMAT_RGB565) || (x_stride == 0) || (y_stride == 0)) {
        return false;
    }

    *out_circles = NULL;
    *out_len = 0;

    uint16_t *labels = NULL;
    uint16_t *parents = NULL;
    omv_esp32_shape_accum_t *accum = NULL;
    size_t roi_w = 0;
    size_t roi_h = 0;
    uint16_t label_count = 0;

    if (!omv_esp32_shape_accumulate(img, roi, x_stride, y_stride, 32,
                                    &labels, &parents, &accum, &roi_w, &roi_h, &label_count)) {
        return false;
    }

    find_circles_list_lnk_data_t *circles = omv_esp32_shape_alloc_cleared(label_count * sizeof(find_circles_list_lnk_data_t));
    if (circles == NULL) {
        omv_esp32_shape_free(accum);
        omv_esp32_shape_free(parents);
        omv_esp32_shape_free(labels);
        return false;
    }

    size_t circles_len = 0;
    for (size_t i = 1; i < label_count; i++) {
        if (!accum[i].used) {
            continue;
        }

        int w = accum[i].x1 - accum[i].x0 + 1;
        int h = accum[i].y1 - accum[i].y0 + 1;
        if ((w < 8) || (h < 8)) {
            continue;
        }

        int delta = omv_esp32_abs_i(w - h);
        int min_wh = IM_MIN(w, h);
        if (delta > (min_wh / 3)) {
            continue;
        }

        unsigned int r = (unsigned int) ((w + h) / 4);
        if (r_step > 1) {
            r = ((r + (r_step / 2)) / r_step) * r_step;
        }
        if ((r < r_min) || (r > r_max)) {
            continue;
        }

        uint32_t magnitude = accum[i].pixels * 64;
        if (magnitude < threshold) {
            continue;
        }

        find_circles_list_lnk_data_t candidate = {0};
        candidate.p.x = accum[i].x0 + (w / 2);
        candidate.p.y = accum[i].y0 + (h / 2);
        candidate.r = r;
        candidate.magnitude = magnitude;

        bool merged = false;
        for (size_t j = 0; j < circles_len; j++) {
            if (omv_esp32_circle_close(&circles[j], &candidate, x_margin, y_margin, r_margin)) {
                if (candidate.magnitude > circles[j].magnitude) {
                    circles[j] = candidate;
                }
                merged = true;
                break;
            }
        }
        if (!merged) {
            circles[circles_len++] = candidate;
        }
    }

    if (circles_len != 0) {
        find_circles_list_lnk_data_t *final_circles =
            omv_esp32_shape_alloc_cleared(circles_len * sizeof(find_circles_list_lnk_data_t));
        if (final_circles == NULL) {
            omv_esp32_shape_free(circles);
            omv_esp32_shape_free(accum);
            omv_esp32_shape_free(parents);
            omv_esp32_shape_free(labels);
            return false;
        }
        memcpy(final_circles, circles, circles_len * sizeof(find_circles_list_lnk_data_t));
        *out_circles = final_circles;
        *out_len = circles_len;
    }

    omv_esp32_shape_free(circles);
    omv_esp32_shape_free(accum);
    omv_esp32_shape_free(parents);
    omv_esp32_shape_free(labels);
    return true;
}

void omv_esp32_free_circles(find_circles_list_lnk_data_t *circles) {
    omv_esp32_shape_free(circles);
}
