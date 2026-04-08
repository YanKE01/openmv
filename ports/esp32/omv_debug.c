/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV monitor debug helpers for ESP32.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "usb_serial_jtag.h"
#include "omv_debug.h"

static void omv_debug_write(const char *str, size_t len) {
    if (len != 0) {
        usb_serial_jtag_tx_strn(str, len);
    }
}

void omv_debug_vprintf(const char *fmt, va_list ap) {
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (len <= 0) {
        return;
    }

    size_t write_len = (size_t) len;
    if (write_len >= sizeof(buf)) {
        write_len = sizeof(buf) - 1;
    }

    omv_debug_write(buf, write_len);
}

void omv_debug_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    omv_debug_vprintf(fmt, ap);
    va_end(ap);
}
