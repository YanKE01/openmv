/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV ESP32 main task glue.
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

#include "py/cstack.h"
#include "py/nlr.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/persistentcode.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "extmod/modmachine.h"
#include "extmod/vfs.h"
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

#if MICROPY_BLUETOOTH_NIMBLE
#include "extmod/modbluetooth.h"
#endif

#if MICROPY_PY_ESPNOW
#include "modespnow.h"
#endif

#include "usbdbg.h"
#include "fb_alloc.h"
#include "framebuffer.h"
#include "mp_utils.h"
#include "omv_camera.h"
#include "omv_gpio.h"
#include "omv_sdcard.h"

// MicroPython runs as a task under FreeRTOS
#define MP_TASK_PRIORITY        (ESP_TASK_PRIO_MIN + 1)

typedef struct _native_code_node_t {
    struct _native_code_node_t *next;
    uint32_t data[];
} native_code_node_t;

static native_code_node_t *native_code_head = NULL;

static void esp_native_code_free_all(void);
static void omv_mount_sdcard_if_present(void);

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
    volatile uint32_t sp = (uint32_t) esp_cpu_get_sp();
    bool first_soft_reset = true;

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
    omv_gpio_init0();
    omv_esp32_camera_init0();
    omv_esp32_camera_init();

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
    mp_cstack_init_with_top((void *) sp, MICROPY_TASK_STACK_SIZE);
    gc_init(mp_task_heap, mp_task_heap + MICROPY_GC_INITIAL_HEAP_SIZE);
    mp_init();
    framebuffer_init0();
    fb_alloc_init0();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    readline_init0();

    machine_pins_init();
    #if MICROPY_PY_MACHINE_I2S
    machine_i2s_init0();
    #endif
    omv_esp32_sdcard_init0();
    omv_mount_sdcard_if_present();

    pyexec_frozen_module("_boot.py", false);

    #if MICROPY_HW_ENABLE_USBDEV
    mp_usbd_init();
    #endif
    usbdbg_init();

    bool interrupted = mp_exec_bootscript("boot.py", true);

    if (interrupted) {
        goto soft_reset_exit;
    }

    if (first_soft_reset && mp_vfs_import_stat("main.py") == MP_IMPORT_STAT_FILE) {
        mp_exec_bootscript("main.py", true);
        goto soft_reset_exit;
    }

    while (!usbdbg_script_ready()) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            usbdbg_set_irq_enabled(true);
            if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
                if (pyexec_raw_repl() != 0) {
                    break;
                }
            } else {
                if (pyexec_friendly_repl() != 0) {
                    break;
                }
            }
            nlr_pop();
        }
    }

    if (usbdbg_script_ready()) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            usbdbg_set_irq_enabled(true);
            pyexec_vstr(usbdbg_get_script(), true);
            usbdbg_set_irq_enabled(false);
            nlr_pop();
        } else {
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t) nlr.ret_val);
        }
    }

soft_reset_exit:
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
    first_soft_reset = false;
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
            esp_partition_register_external(esp_flash_default_chip, offset, size, "vfs",
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
        }
    }
}

void MICROPY_ESP_IDF_ENTRY(void) {
    MICROPY_BOARD_STARTUP();
    xTaskCreatePinnedToCore(mp_task, "mp_task", MICROPY_TASK_STACK_SIZE / sizeof(StackType_t),
        NULL, MP_TASK_PRIORITY, &mp_main_task_handle, MP_TASK_COREID);
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

static void omv_mount_sdcard_if_present(void) {
    #if MICROPY_HW_ENABLE_SDCARD
    const char *path = "/sdcard";
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        const char *lookup_path = path;
        if (mp_vfs_lookup_path(path, &lookup_path) != MP_VFS_NONE) {
            mp_vfs_umount(mp_obj_new_str("/sdcard", 7));
        }

        mp_obj_t sdcard_kwargs[] = {
            MP_OBJ_NEW_QSTR(MP_QSTR_slot),
            MP_OBJ_NEW_SMALL_INT(0),
            MP_OBJ_NEW_QSTR(MP_QSTR_width),
            MP_OBJ_NEW_SMALL_INT(4),
        };
        mp_obj_t sdcard = mp_call_function_n_kw(MP_OBJ_FROM_PTR(&machine_sdcard_type), 0, 2, sdcard_kwargs);
        mp_obj_t mount_args[] = {
            sdcard,
            mp_obj_new_str("/sdcard", 7),
        };
        mp_vfs_mount(2, mount_args, (mp_map_t *) &mp_const_empty_map);
        nlr_pop();
    } else {
        // Ignore missing/unusable SD card and continue boot.
    }
    #endif
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
        mp_native_relocate(reloc, buf, (uintptr_t) p);
    }
    memcpy(p, buf, len);
    return p;
}
