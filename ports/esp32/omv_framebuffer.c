/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal framebuffer support for the OpenMV ESP32 port.
 */

#include <string.h>
#include <stddef.h>

#include "driver/jpeg_encode.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"

#include "py/mphal.h"
#include "py/runtime.h"

#include "framebuffer.h"
#include "omv_boardconfig.h"
#include "omv_protocol.h"
#include "omv_test_preview.h"

static framebuffer_t framebuffers[FB_MAX_ID];
static void *fb_main_mem = NULL;
static void *fb_stream_mem = NULL;
static const size_t omv_esp32_stream_header_size = offsetof(framebuffer_header_t, data);
static jpeg_encoder_handle_t omv_esp32_fb_jpeg_handle = NULL;
static uint8_t *omv_esp32_fb_jpeg_in = NULL;
static size_t omv_esp32_fb_jpeg_in_size = 0;
static uint8_t *omv_esp32_fb_jpeg_out = NULL;
static size_t omv_esp32_fb_jpeg_out_size = 0;

static void *omv_esp32_framebuffer_alloc(size_t size) {
    void *ptr = NULL;

    if (esp_psram_is_initialized()) {
        ptr = heap_caps_aligned_alloc(FRAMEBUFFER_ALIGNMENT, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }

    if (ptr == NULL) {
        ptr = heap_caps_aligned_alloc(FRAMEBUFFER_ALIGNMENT, size, MALLOC_CAP_8BIT);
    }

    return ptr;
}

static bool omv_esp32_framebuffer_jpeg_init(void) {
    if (omv_esp32_fb_jpeg_handle != NULL && omv_esp32_fb_jpeg_in != NULL && omv_esp32_fb_jpeg_out != NULL) {
        return true;
    }

    if (omv_esp32_fb_jpeg_handle == NULL) {
        jpeg_encode_engine_cfg_t encode_eng_cfg = {
            .intr_priority = 0,
            .timeout_ms = 200,
        };
        if (jpeg_new_encoder_engine(&encode_eng_cfg, &omv_esp32_fb_jpeg_handle) != ESP_OK) {
            omv_esp32_fb_jpeg_handle = NULL;
            return false;
        }
    }

    if (omv_esp32_fb_jpeg_in == NULL) {
        jpeg_encode_memory_alloc_cfg_t input_cfg = {
            .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER,
        };
        omv_esp32_fb_jpeg_in = jpeg_alloc_encoder_mem(OMV_FB_SIZE, &input_cfg, &omv_esp32_fb_jpeg_in_size);
        if (omv_esp32_fb_jpeg_in == NULL) {
            return false;
        }
    }

    if (omv_esp32_fb_jpeg_out == NULL) {
        jpeg_encode_memory_alloc_cfg_t output_cfg = {
            .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
        };
        omv_esp32_fb_jpeg_out = jpeg_alloc_encoder_mem(OMV_SB_SIZE - omv_esp32_stream_header_size,
                                                       &output_cfg,
                                                       &omv_esp32_fb_jpeg_out_size);
        if (omv_esp32_fb_jpeg_out == NULL) {
            return false;
        }
    }

    return true;
}

void framebuffer_init(framebuffer_t *fb, void *buff, size_t size, bool dynamic, bool enabled) {
    memset(fb, 0, sizeof(*fb));
    fb->raw_size = size;
    fb->raw_base = buff;
    fb->dynamic = dynamic;
    fb->enabled = enabled;
    fb->quality = ((OMV_JPEG_QUALITY_HIGH - OMV_JPEG_QUALITY_LOW) / 2) + OMV_JPEG_QUALITY_LOW;
    mutex_init0(&fb->lock);
}

void framebuffer_init0() {
    bool stream_enabled = framebuffer_get(FB_STREAM_ID)->enabled;

    if (fb_main_mem == NULL) {
        fb_main_mem = omv_esp32_framebuffer_alloc(OMV_FB_SIZE);
    }
    if (fb_stream_mem == NULL) {
        fb_stream_mem = omv_esp32_framebuffer_alloc(OMV_SB_SIZE);
    }

    if ((fb_main_mem == NULL) || (fb_stream_mem == NULL)) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("failed to allocate framebuffer memory"));
    }

    framebuffer_init(framebuffer_get(FB_MAINFB_ID), fb_main_mem, OMV_FB_SIZE, true, true);
    framebuffer_init(framebuffer_get(FB_STREAM_ID), fb_stream_mem, OMV_SB_SIZE, true, stream_enabled);
    memset(framebuffer_get(FB_STREAM_ID)->raw_base, 0, omv_esp32_stream_header_size);
}

void framebuffer_to_image(framebuffer_t *fb, image_t *img) {
    if (img == NULL) {
        return;
    }

    img->w = fb->w;
    img->h = fb->h;
    img->pixfmt = fb->pixfmt;
    img->size = fb->size;
    img->pixels = (uint8_t *) fb->raw_base;
}

void framebuffer_from_image(framebuffer_t *fb, image_t *img) {
    if (img == NULL) {
        fb->w = 0;
        fb->h = 0;
        fb->size = 0;
        fb->pixfmt = PIXFORMAT_INVALID;
        return;
    }

    fb->w = img->w;
    fb->h = img->h;
    fb->size = img->size;
    fb->pixfmt = img->pixfmt;
}

framebuffer_t *framebuffer_get(size_t id) {
    if (id >= FB_MAX_ID) {
        return NULL;
    }
    return &framebuffers[id];
}

char *framebuffer_pool_end(framebuffer_t *fb) {
    return fb->raw_base;
}

void framebuffer_flush(framebuffer_t *fb) {
    fb->w = 0;
    fb->h = 0;
    fb->size = 0;
    fb->pixfmt = PIXFORMAT_INVALID;
    if (fb->raw_base != NULL) {
        memset(fb->raw_base, 0, IM_MIN(fb->raw_size, omv_esp32_stream_header_size));
    }
}

int framebuffer_resize(framebuffer_t *fb, size_t count, size_t frame_size, bool expand) {
    (void) fb;
    (void) count;
    (void) frame_size;
    (void) expand;
    return -1;
}

bool framebuffer_writable(framebuffer_t *fb) {
    (void) fb;
    return false;
}

bool framebuffer_readable(framebuffer_t *fb) {
    (void) fb;
    return false;
}

vbuffer_t *framebuffer_acquire(framebuffer_t *fb, uint32_t flags) {
    (void) fb;
    (void) flags;
    return NULL;
}

vbuffer_t *framebuffer_release(framebuffer_t *fb, uint32_t flags) {
    (void) fb;
    (void) flags;
    return NULL;
}

void framebuffer_update_preview(image_t *src) {
    framebuffer_t *fb = framebuffer_get(FB_STREAM_ID);
    framebuffer_header_t *header;
    jpeg_encode_cfg_t encode_cfg;
    uint32_t jpeg_size = 0;
    size_t src_size;
    jpeg_enc_input_format_t src_type;
    jpeg_down_sampling_type_t sub_sample;

    if ((src == NULL) || (fb == NULL) || !fb->enabled || !omv_protocol_is_active()) {
        return;
    }

    if ((src->pixels == NULL) || (src->w <= 0) || (src->h <= 0)) {
        return;
    }

    switch (src->pixfmt) {
        case PIXFORMAT_RGB565:
            src_size = (size_t) src->w * (size_t) src->h * sizeof(uint16_t);
            src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
            sub_sample = JPEG_DOWN_SAMPLING_YUV422;
            break;
        case PIXFORMAT_GRAYSCALE:
            src_size = (size_t) src->w * (size_t) src->h;
            src_type = JPEG_ENCODE_IN_FORMAT_GRAY;
            sub_sample = JPEG_DOWN_SAMPLING_GRAY;
            break;
        default:
            return;
    }

    if (!omv_esp32_framebuffer_jpeg_init()) {
        return;
    }

    if ((src_size == 0) || (src_size > omv_esp32_fb_jpeg_in_size)) {
        return;
    }

    if (!mutex_try_lock(&fb->lock, MUTEX_TID_OMV)) {
        return;
    }

    memcpy(omv_esp32_fb_jpeg_in, src->pixels, src_size);

    encode_cfg.width = src->w;
    encode_cfg.height = src->h;
    encode_cfg.src_type = src_type;
    encode_cfg.sub_sample = sub_sample;
    encode_cfg.image_quality = OMV_JPEG_QUALITY_LOW;

    if (jpeg_encoder_process(omv_esp32_fb_jpeg_handle,
                             &encode_cfg,
                             omv_esp32_fb_jpeg_in,
                             src_size,
                             omv_esp32_fb_jpeg_out,
                             omv_esp32_fb_jpeg_out_size,
                             &jpeg_size) == ESP_OK &&
        jpeg_size != 0 &&
        jpeg_size <= (fb->raw_size - omv_esp32_stream_header_size)) {
        header = (framebuffer_header_t *) fb->raw_base;
        memcpy(header->data, omv_esp32_fb_jpeg_out, jpeg_size);

        fb->w = src->w;
        fb->h = src->h;
        fb->pixfmt = PIXFORMAT_JPEG;
        fb->size = jpeg_size;

        header->width = fb->w;
        header->height = fb->h;
        header->pixfmt = fb->pixfmt;
        header->size = fb->size;
        header->offset = omv_esp32_stream_header_size;

        omv_esp32_test_preview_hold(100);
        omv_protocol_send_event(OMV_PROTOCOL_CHANNEL_ID_STREAM, OMV_PROTOCOL_EVENT_NOTIFY, false);
    }

    mutex_unlock(&fb->lock, MUTEX_TID_OMV);
}
