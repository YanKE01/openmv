/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal framebuffer support for the OpenMV ESP32 port.
 */

#include <string.h>
#include <stddef.h>

#include "esp_heap_caps.h"
#include "esp_psram.h"

#include "py/runtime.h"

#include "framebuffer.h"
#include "omv_boardconfig.h"

static framebuffer_t framebuffers[FB_MAX_ID];
static void *fb_main_mem = NULL;
static void *fb_stream_mem = NULL;
static const size_t omv_esp32_stream_header_size = offsetof(framebuffer_header_t, data);

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
    (void) src;
}
