/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 * Copyright (c) 2026 OpenMV, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_task.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "esp_partition.h"

#include "py/cstack.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/persistentcode.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "extmod/modmachine.h"
#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"
#include "shared/timeutils/timeutils.h"
#include "shared/tinyusb/mp_usbd.h"
#include "mbedtls/platform_time.h"

#include "uart.h"
#include "usb.h"
#include "usb_serial_jtag.h"
#include "modesp32.h"
#include "modmachine.h"
#include "modnetwork.h"
#include "framebuffer.h"
#include "omv_camera.h"
#include "omv_protocol.h"
#include "omv_test_preview.h"

#if MICROPY_BLUETOOTH_NIMBLE
#include "extmod/modbluetooth.h"
#endif

#if MICROPY_PY_ESPNOW
#include "modespnow.h"
#endif

#define MP_TASK_PRIORITY        (ESP_TASK_PRIO_MIN + 1)

typedef struct _native_code_node_t {
    struct _native_code_node_t *next;
    uint32_t data[];
} native_code_node_t;

static native_code_node_t *native_code_head = NULL;

static void esp_native_code_free_all(void);

static int omv_protocol_init_esp32(void) {
    const omv_protocol_config_t config = {
        .crc_enabled = true,
        .seq_enabled = true,
        .ack_enabled = true,
        .event_enabled = true,
        .max_payload = OMV_PROTOCOL_MAX_PAYLOAD_SIZE,
        .soft_reboot = true,
        .rtx_retries = OMV_PROTOCOL_DEF_RTX_RETRIES,
        .rtx_timeout_ms = OMV_PROTOCOL_DEF_RTX_TIMEOUT_MS,
        .lock_intval_ms = OMV_PROTOCOL_MIN_LOCK_INTERVAL_MS,
    };

    if (omv_protocol_init(&config) != 0) {
        return -1;
    }
    if (omv_protocol_register_channel(&omv_usb_channel) < 0) {
        return -1;
    }
    if (omv_protocol_register_channel(&omv_stdin_channel) < 0) {
        return -1;
    }
    if (omv_protocol_register_channel(&omv_stdout_channel) < 0) {
        return -1;
    }
    if (omv_protocol_register_channel(&omv_stream_channel) < 0) {
        return -1;
    }
    return 0;
}

int vprintf_null(const char *format, va_list ap) {
    return 0;
}

#if MICROPY_SSL_MBEDTLS
static time_t platform_mbedtls_time(time_t *timer) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + TIMEUTILS_SECONDS_1970_TO_2000;
}
#endif

void mp_task(void *pvParameter) {
    volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();
    #if MICROPY_PY_THREAD
    mp_thread_init(pxTaskGetStackStart(NULL), MICROPY_TASK_STACK_SIZE / sizeof(uintptr_t));
    #endif
    #if MICROPY_HW_ESP_USB_SERIAL_JTAG
    usb_serial_jtag_init();
    #endif
    #if MICROPY_HW_ENABLE_USBDEV
    usb_phy_init();
    #endif
    #if MICROPY_HW_ENABLE_UART_REPL
    uart_stdout_init();
    #endif
    machine_init();

    #if MICROPY_SSL_MBEDTLS
    mbedtls_platform_set_time(platform_mbedtls_time);
    #endif

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE("esp_init", "can't create event loop: 0x%x\n", err);
    }

    void *mp_task_heap = MP_PLAT_ALLOC_HEAP(MICROPY_GC_INITIAL_HEAP_SIZE);
    if (mp_task_heap == NULL) {
        printf("mp_task_heap allocation failed!\n");
        esp_restart();
    }

soft_reset:
    mp_cstack_init_with_top((void *)sp, MICROPY_TASK_STACK_SIZE);
    gc_init(mp_task_heap, mp_task_heap + MICROPY_GC_INITIAL_HEAP_SIZE);
    mp_init();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    readline_init0();
    framebuffer_init0();
    omv_esp32_camera_init0();
    omv_esp32_test_preview_init0();

    machine_pins_init();
    #if MICROPY_PY_MACHINE_I2S
    machine_i2s_init0();
    #endif

    if (omv_protocol_init_esp32() != 0) {
        mp_printf(&mp_plat_print, "Failed to init OpenMV protocol\r\n");
        goto soft_reset_exit;
    }

    if (omv_esp32_camera_init() != 0) {
        mp_printf(&mp_plat_print, "Failed to init ESP32 camera\r\n");
    }

    pyexec_frozen_module("_boot.py", false);
    int ret = pyexec_file_if_exists("boot.py");

    #if MICROPY_HW_ENABLE_USBDEV
    mp_usbd_init();
    #endif

    if (ret & PYEXEC_FORCED_EXIT) {
        goto soft_reset_exit;
    }
    if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL && ret != 0) {
        ret = pyexec_file_if_exists("main.py");
        if (ret & PYEXEC_FORCED_EXIT) {
            goto soft_reset_exit;
        }
    }

    while (!omv_protocol_exec_script()) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_hal_set_interrupt_char(CHAR_CTRL_C);
            if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
                vprintf_like_t vprintf_log = esp_log_set_vprintf(vprintf_null);
                int repl_ret = pyexec_raw_repl();
                esp_log_set_vprintf(vprintf_log);
                if (repl_ret != 0) {
                    nlr_pop();
                    break;
                }
            } else {
                if (pyexec_friendly_repl() != 0) {
                    nlr_pop();
                    break;
                }
            }
            nlr_pop();
        } else {
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        }
    }

soft_reset_exit:
    mp_hal_set_interrupt_char(-1);

    #if MICROPY_BLUETOOTH_NIMBLE
    mp_bluetooth_deinit();
    #endif

    #if MICROPY_PY_ESPNOW
    espnow_deinit(mp_const_none);
    MP_STATE_PORT(espnow_singleton) = NULL;
    #endif

    #if MICROPY_PY_MACHINE_UART
    machine_uart_deinit_all();
    #endif
    machine_timer_deinit_all();

    #if MICROPY_PY_ESP32_PCNT
    esp32_pcnt_deinit_all();
    #endif

    #if MICROPY_PY_THREAD
    mp_thread_deinit();
    #endif

    omv_protocol_deinit();
    omv_esp32_camera_deinit();

    #if MICROPY_HW_ENABLE_USBDEV
    mp_usbd_deinit();
    #endif

    gc_sweep_all();
    esp_native_code_free_all();

    mp_hal_stdout_tx_str("MPY: soft reboot\r\n");

    #if MICROPY_PY_MACHINE_PWM
    machine_pwm_deinit_all();
    #endif
    machine_pins_deinit();
    #if MICROPY_PY_MACHINE_I2C_TARGET
    mp_machine_i2c_target_deinit_all();
    #endif
    machine_deinit();

    #if MICROPY_PY_SOCKET_EVENTS
    socket_events_deinit();
    #endif

    mp_deinit();
    fflush(stdout);

    goto soft_reset;
}

void boardctrl_startup(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_flash_get_physical_size(NULL, &esp_flash_default_chip->size);

    if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vfs") == NULL
        && esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ffat") == NULL) {
        size_t offset = 0;
        esp_partition_iterator_t iter = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
        while (iter != NULL) {
            const esp_partition_t *part = esp_partition_get(iter);
            offset = MAX(offset, part->address + part->size);
            iter = esp_partition_next(iter);
        }

        if (offset > 0 && esp_flash_default_chip->size > offset) {
            size_t size = esp_flash_default_chip->size - offset;
            esp_partition_register_external(esp_flash_default_chip, offset, size, "vfs", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
        }
    }
}

void MICROPY_ESP_IDF_ENTRY(void) {
    MICROPY_BOARD_STARTUP();
    xTaskCreatePinnedToCore(mp_task, "mp_task", MICROPY_TASK_STACK_SIZE / sizeof(StackType_t), NULL, MP_TASK_PRIORITY, &mp_main_task_handle, MP_TASK_COREID);
}

MP_WEAK void nlr_jump_fail(void *val) {
    printf("NLR jump failed, val=%p\n", val);
    esp_restart();
}

static void esp_native_code_free_all(void) {
    while (native_code_head != NULL) {
        native_code_node_t *next = native_code_head->next;
        heap_caps_free(native_code_head);
        native_code_head = next;
    }
}

void *esp_native_code_commit(void *buf, size_t len, void *reloc) {
    len = (len + 3) & ~3;
    size_t len_node = sizeof(native_code_node_t) + len;
    native_code_node_t *node = heap_caps_malloc(len_node, MALLOC_CAP_EXEC);
    #if CONFIG_IDF_TARGET_ESP32S2
    if (node != NULL && !esp_ptr_executable(node)) {
        free(node);
        node = NULL;
    }
    #endif
    if (node == NULL) {
        m_malloc_fail(len_node);
    }
    node->next = native_code_head;
    native_code_head = node;
    void *p = node->data;
    if (reloc) {
        mp_native_relocate(reloc, buf, (uintptr_t)p);
    }
    memcpy(p, buf, len);
    return p;
}
