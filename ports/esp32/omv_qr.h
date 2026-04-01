/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_QR_H__
#define __OMV_ESP32_QR_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imlib.h"

typedef struct {
    int x;
    int y;
} omv_qr_point_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} omv_qr_rect_t;

typedef struct {
    omv_qr_point_t corners[4];
    omv_qr_rect_t rect;
    size_t payload_len;
    char *payload;
    uint8_t version;
    uint8_t ecc_level;
    uint8_t mask;
    uint8_t data_type;
    uint32_t eci;
} omv_qrcode_t;

typedef struct {
    size_t count;
    omv_qrcode_t *items;
} omv_qrcode_list_t;

bool omv_qr_find_qrcodes(omv_qrcode_list_t *out, const rectangle_t *roi);
void omv_qr_free_qrcodes(omv_qrcode_list_t *out);

#endif // __OMV_ESP32_QR_H__
