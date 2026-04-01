/*
 * SPDX-License-Identifier: MIT
 *
 * Camera-backed preview publisher for Phase 3.1 bring-up.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "driver/jpeg_encode.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "framebuffer.h"
#include "omv_camera.h"
#include "omv_boardconfig.h"
#include "omv_protocol.h"

#define OMV_ESP32_TEST_PREVIEW_WIDTH   (320)
#define OMV_ESP32_TEST_PREVIEW_HEIGHT  (240)
#define OMV_ESP32_TEST_PREVIEW_FPS     (30)
#define OMV_ESP32_TEST_PREVIEW_QUALITY (50)
#define OMV_ESP32_TEST_PREVIEW_TIMEOUT_MS (200)
#define OMV_ESP32_TEST_PREVIEW_STACK_SIZE (12288)

static TaskHandle_t omv_esp32_test_preview_task_handle = NULL;
static jpeg_encoder_handle_t omv_esp32_jpeg_handle = NULL;
static uint16_t *omv_esp32_test_preview_rgb565 = NULL;
static uint8_t *omv_esp32_test_preview_jpeg = NULL;
static size_t omv_esp32_test_preview_jpeg_size = 0;
static const size_t omv_esp32_stream_header_size = offsetof(framebuffer_header_t, data);
static volatile TickType_t omv_esp32_test_preview_hold_until = 0;

static bool omv_esp32_test_preview_jpeg_init(void) {
    if (omv_esp32_jpeg_handle != NULL) {
        return true;
    }

    jpeg_encode_engine_cfg_t encode_eng_cfg = {
        .intr_priority = 0,
        .timeout_ms = OMV_ESP32_TEST_PREVIEW_TIMEOUT_MS,
    };

    if (jpeg_new_encoder_engine(&encode_eng_cfg, &omv_esp32_jpeg_handle) != ESP_OK) {
        omv_esp32_jpeg_handle = NULL;
        return false;
    }

    return true;
}

static bool omv_esp32_fill_jpeg_stream_frame(framebuffer_t *fb, uint16_t *pixels) {
    uint32_t width = omv_esp32_camera_get_width();
    uint32_t height = omv_esp32_camera_get_height();
    size_t frame_size = (size_t) width * (size_t) height * sizeof(uint16_t);

    if (fb == NULL || pixels == NULL || omv_esp32_jpeg_handle == NULL ||
        omv_esp32_test_preview_jpeg == NULL || omv_esp32_test_preview_jpeg_size == 0 ||
        width == 0 || height == 0) {
        return false;
    }

    uint32_t jpeg_size = 0;
    jpeg_encode_cfg_t encode_cfg = {
        .width = width,
        .height = height,
        .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = OMV_ESP32_TEST_PREVIEW_QUALITY,
    };

    if (jpeg_encoder_process(omv_esp32_jpeg_handle,
                             &encode_cfg,
                             (const uint8_t *) pixels,
                             frame_size,
                             omv_esp32_test_preview_jpeg,
                             omv_esp32_test_preview_jpeg_size,
                             &jpeg_size) != ESP_OK) {
        return false;
    }

    framebuffer_header_t *header = (framebuffer_header_t *) fb->raw_base;
    uint8_t *frame_data = header->data;
    size_t available = fb->raw_size - omv_esp32_stream_header_size;

    if (jpeg_size == 0 || jpeg_size > available) {
        return false;
    }

    memcpy(frame_data, omv_esp32_test_preview_jpeg, jpeg_size);

    fb->w = width;
    fb->h = height;
    fb->pixfmt = PIXFORMAT_JPEG;
    fb->size = jpeg_size;

    header->width = fb->w;
    header->height = fb->h;
    header->pixfmt = fb->pixfmt;
    header->size = fb->size;
    header->offset = omv_esp32_stream_header_size;
    return true;
}

static void omv_esp32_test_preview_task(void *arg) {
    (void) arg;

    for (;;) {
        if ((int32_t) (omv_esp32_test_preview_hold_until - xTaskGetTickCount()) > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        framebuffer_t *fb = framebuffer_get(FB_STREAM_ID);

        if (fb != NULL &&
            fb->enabled &&
            fb->raw_base != NULL &&
            fb->raw_size > omv_esp32_stream_header_size &&
            omv_esp32_camera_is_ready() &&
            mutex_try_lock_fair(&fb->lock, MUTEX_TID_OMV)) {
            if (omv_esp32_camera_capture_rgb565(
                    omv_esp32_test_preview_rgb565,
                    ((size_t) OMV_ESP32_TEST_PREVIEW_WIDTH * OMV_ESP32_TEST_PREVIEW_HEIGHT)) &&
                omv_esp32_fill_jpeg_stream_frame(fb, omv_esp32_test_preview_rgb565)) {
                if (omv_protocol_is_active()) {
                    omv_protocol_send_event(OMV_PROTOCOL_CHANNEL_ID_STREAM, OMV_PROTOCOL_EVENT_NOTIFY, false);
                }
            } else {
                framebuffer_flush(fb);
            }

            mutex_unlock(&fb->lock, MUTEX_TID_OMV);
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / OMV_ESP32_TEST_PREVIEW_FPS));
    }
}

void omv_esp32_test_preview_init0(void) {
#ifdef OMV_PY_IMAGE_ESP32_MINIMAL
    return;
#endif

    if (omv_esp32_test_preview_task_handle != NULL) {
        return;
    }

    size_t rgb565_buffer_size = 0;
    jpeg_encode_memory_alloc_cfg_t jpeg_input_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER,
    };
    omv_esp32_test_preview_rgb565 = jpeg_alloc_encoder_mem(
        OMV_ESP32_TEST_PREVIEW_WIDTH *
        OMV_ESP32_TEST_PREVIEW_HEIGHT *
        sizeof(uint16_t),
        &jpeg_input_mem_cfg,
        &rgb565_buffer_size);

    jpeg_encode_memory_alloc_cfg_t jpeg_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    omv_esp32_test_preview_jpeg = jpeg_alloc_encoder_mem(
        OMV_SB_SIZE - omv_esp32_stream_header_size,
        &jpeg_mem_cfg,
        &omv_esp32_test_preview_jpeg_size);

    if (omv_esp32_test_preview_rgb565 == NULL) {
        return;
    }

    if ((omv_esp32_test_preview_jpeg == NULL) ||
        !omv_esp32_test_preview_jpeg_init()) {
        omv_esp32_test_preview_jpeg = NULL;
        omv_esp32_test_preview_jpeg_size = 0;
    }

    xTaskCreate(omv_esp32_test_preview_task,
                "omv_preview",
                OMV_ESP32_TEST_PREVIEW_STACK_SIZE,
                NULL,
                tskIDLE_PRIORITY + 1,
                &omv_esp32_test_preview_task_handle);
}

void omv_esp32_test_preview_hold(uint32_t ms) {
    omv_esp32_test_preview_hold_until = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
}
