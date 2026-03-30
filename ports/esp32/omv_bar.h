/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_BAR_H__
#define __OMV_ESP32_BAR_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int x;
    int y;
} omv_bar_point_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} omv_bar_rect_t;

typedef struct {
    omv_bar_point_t corners[4];
    omv_bar_rect_t rect;
    size_t payload_len;
    char *payload;
    uint16_t type;
    uint16_t rotation;
    int quality;
} omv_barcode_t;

typedef struct {
    size_t count;
    omv_barcode_t *items;
} omv_barcode_list_t;

bool omv_bar_find_barcodes(omv_barcode_list_t *out);
void omv_bar_free_barcodes(omv_barcode_list_t *out);

#endif // __OMV_ESP32_BAR_H__
