/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV ESP32 camera integration.
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "driver/ppa.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_cache_private.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"

#include "common/mutex.h"
#include "imlib.h"
#include "omv_camera.h"
#include "omv_csi.h"
#include "omv_debug.h"

#define OMV_ESP32_CAMERA_INPUT_WIDTH            (1280)
#define OMV_ESP32_CAMERA_INPUT_HEIGHT           (720)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_WIDTH     (640)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_HEIGHT    (480)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_OFFSET_X  (320)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_OFFSET_Y  (120)
#define OMV_ESP32_CAMERA_OUTPUT_QQVGA_WIDTH     (160)
#define OMV_ESP32_CAMERA_OUTPUT_QQVGA_HEIGHT    (120)
#define OMV_ESP32_CAMERA_OUTPUT_QVGA_WIDTH      (320)
#define OMV_ESP32_CAMERA_OUTPUT_QVGA_HEIGHT     (240)
#define OMV_ESP32_CAMERA_BUFFER_COUNT           (2)
#define OMV_ESP32_CAMERA_SCCB_I2C_PORT          (0)
#define OMV_ESP32_CAMERA_SCCB_I2C_SCL_PIN       (13)
#define OMV_ESP32_CAMERA_SCCB_I2C_SDA_PIN       (14)
#define OMV_ESP32_CAMERA_SCCB_I2C_FREQ          (100000)
#define OMV_ESP32_CAMERA_SENSOR_RESET_PIN       (26)
#define OMV_ESP32_CAMERA_SENSOR_PWDN_PIN        (12)
#define OMV_ESP32_CAMERA_XCLK_PIN               (11)
#define OMV_ESP32_CAMERA_XCLK_FREQ              (24000000)

typedef struct {
    void *ptr;
    size_t len;
} omv_esp32_camera_buffer_t;

typedef struct {
    bool initialized;
    bool video_inited;
    bool streaming;
    bool hmirror;
    bool vflip;
    int fd;
    uint32_t input_width;
    uint32_t input_height;
    uint32_t active_input_width;
    uint32_t active_input_height;
    uint32_t active_input_offset_x;
    uint32_t active_input_offset_y;
    uint32_t width;
    uint32_t height;
    uint32_t pixfmt;
    uint32_t output_pixfmt;
    size_t ppa_out_size;
    ppa_client_handle_t ppa_handle;
    esp_cam_sensor_xclk_handle_t xclk_handle;
    uint8_t *ppa_out_buf;
    mutex_t lock;
    omv_esp32_camera_buffer_t buffers[OMV_ESP32_CAMERA_BUFFER_COUNT];
} omv_esp32_camera_t;

static omv_esp32_camera_t camera_ctx;

static void omv_esp32_camera_release_ppa(void);
static int omv_esp32_camera_init_ppa(void);

static bool omv_esp32_camera_framesize_to_dimensions(uint32_t framesize, uint32_t *width, uint32_t *height) {
    if ((width == NULL) || (height == NULL)) {
        return false;
    }

    switch (framesize) {
        case OMV_CSI_FRAMESIZE_QQVGA:
            *width = OMV_ESP32_CAMERA_OUTPUT_QQVGA_WIDTH;
            *height = OMV_ESP32_CAMERA_OUTPUT_QQVGA_HEIGHT;
            return true;
        case OMV_CSI_FRAMESIZE_QVGA:
            *width = OMV_ESP32_CAMERA_OUTPUT_QVGA_WIDTH;
            *height = OMV_ESP32_CAMERA_OUTPUT_QVGA_HEIGHT;
            return true;
        default:
            return false;
    }
}

static size_t omv_esp32_camera_align_up(size_t value, size_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

static size_t omv_esp32_camera_output_bpp(uint32_t pixformat) {
    return (pixformat == PIXFORMAT_GRAYSCALE) ? sizeof(uint8_t) : sizeof(uint16_t);
}

static size_t omv_esp32_camera_output_size(uint32_t width, uint32_t height, uint32_t pixformat) {
    return (size_t) width * (size_t) height * omv_esp32_camera_output_bpp(pixformat);
}

static ppa_srm_color_mode_t omv_esp32_camera_output_color_mode(uint32_t pixformat) {
    return (pixformat == PIXFORMAT_GRAYSCALE) ? PPA_SRM_COLOR_MODE_GRAY8 : PPA_SRM_COLOR_MODE_RGB565;
}

static bool omv_esp32_camera_reinit_ppa_locked(void) {
    omv_esp32_camera_release_ppa();
    return omv_esp32_camera_init_ppa() == 0;
}

static void omv_esp32_camera_stop_xclk(void) {
    if (camera_ctx.xclk_handle != NULL) {
        esp_cam_sensor_xclk_stop(camera_ctx.xclk_handle);
        esp_cam_sensor_xclk_free(camera_ctx.xclk_handle);
        camera_ctx.xclk_handle = NULL;
    }
}

static bool omv_esp32_camera_start_xclk(void) {
    esp_cam_sensor_xclk_config_t xclk_config = {
        .esp_clock_router_cfg = {
            .xclk_pin = OMV_ESP32_CAMERA_XCLK_PIN,
            .xclk_freq_hz = OMV_ESP32_CAMERA_XCLK_FREQ,
        },
    };

    esp_err_t ret = esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &camera_ctx.xclk_handle);
    if (ret != ESP_OK) {
        OMV_DEBUG("[OMV] camera xclk alloc failed: %s\r\n", esp_err_to_name(ret));
        camera_ctx.xclk_handle = NULL;
        return false;
    }

    ret = esp_cam_sensor_xclk_start(camera_ctx.xclk_handle, &xclk_config);
    if (ret != ESP_OK) {
        OMV_DEBUG("[OMV] camera xclk start failed: %s\r\n", esp_err_to_name(ret));
        omv_esp32_camera_stop_xclk();
        return false;
    }

    return true;
}

static bool omv_esp32_camera_dequeue_buffer(struct v4l2_buffer *buf) {
    if (buf == NULL) {
        return false;
    }

    memset(buf, 0, sizeof(*buf));
    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_MMAP;

    if (ioctl(camera_ctx.fd, VIDIOC_DQBUF, buf) != 0) {
        return false;
    }

    if (buf->index >= OMV_ESP32_CAMERA_BUFFER_COUNT ||
        camera_ctx.buffers[buf->index].ptr == NULL ||
        (buf->flags & V4L2_BUF_FLAG_ERROR)) {
        if (buf->index < OMV_ESP32_CAMERA_BUFFER_COUNT) {
            ioctl(camera_ctx.fd, VIDIOC_QBUF, buf);
        }
        return false;
    }

    return true;
}

static bool omv_esp32_camera_queue_buffer(struct v4l2_buffer *buf) {
    if (buf == NULL) {
        return false;
    }

    return ioctl(camera_ctx.fd, VIDIOC_QBUF, buf) == 0;
}

static void omv_esp32_camera_release_buffers(void) {
    for (size_t i = 0; i < OMV_ESP32_CAMERA_BUFFER_COUNT; i++) {
        if (camera_ctx.buffers[i].ptr != NULL) {
            munmap(camera_ctx.buffers[i].ptr, camera_ctx.buffers[i].len);
            camera_ctx.buffers[i].ptr = NULL;
            camera_ctx.buffers[i].len = 0;
        }
    }
}

static int omv_esp32_camera_open_device(void) {
    struct v4l2_capability capability = {0};

    camera_ctx.fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    if (camera_ctx.fd < 0) {
        OMV_DEBUG("[OMV] camera open %s failed\r\n", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return -1;
    }

    if (ioctl(camera_ctx.fd, VIDIOC_QUERYCAP, &capability) != 0) {
        OMV_DEBUG("[OMV] camera querycap failed\r\n");
        close(camera_ctx.fd);
        camera_ctx.fd = -1;
        return -1;
    }

    return 0;
}

static int omv_esp32_camera_set_format(void) {
    struct v4l2_format format;

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = OMV_ESP32_CAMERA_INPUT_WIDTH;
    format.fmt.pix.height = OMV_ESP32_CAMERA_INPUT_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;

    if (ioctl(camera_ctx.fd, VIDIOC_S_FMT, &format) != 0) {
        OMV_DEBUG("[OMV] camera set fmt failed\r\n");
        return -1;
    }

    camera_ctx.input_width = format.fmt.pix.width;
    camera_ctx.input_height = format.fmt.pix.height;
    camera_ctx.pixfmt = format.fmt.pix.pixelformat;
    return 0;
}

static void omv_esp32_camera_release_ppa(void) {
    if (camera_ctx.ppa_handle != NULL) {
        ppa_unregister_client(camera_ctx.ppa_handle);
        camera_ctx.ppa_handle = NULL;
    }
    if (camera_ctx.ppa_out_buf != NULL) {
        heap_caps_free(camera_ctx.ppa_out_buf);
        camera_ctx.ppa_out_buf = NULL;
    }
    camera_ctx.ppa_out_size = 0;
}

static int omv_esp32_camera_init_ppa(void) {
    esp_err_t ret;
    size_t cache_align = 0;
    size_t out_size;
    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };

    ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &cache_align);
    if ((ret != ESP_OK) || (cache_align == 0)) {
        OMV_DEBUG("[OMV] camera get cache align failed\r\n");
        return -1;
    }

    if ((camera_ctx.width == 0) || (camera_ctx.height == 0)) {
        camera_ctx.width = OMV_ESP32_CAMERA_OUTPUT_QVGA_WIDTH;
        camera_ctx.height = OMV_ESP32_CAMERA_OUTPUT_QVGA_HEIGHT;
    }

    out_size = omv_esp32_camera_output_size(camera_ctx.width, camera_ctx.height, camera_ctx.output_pixfmt);

    ret = ppa_register_client(&ppa_cfg, &camera_ctx.ppa_handle);
    if (ret != ESP_OK) {
        OMV_DEBUG("[OMV] camera ppa init failed: %s\r\n", esp_err_to_name(ret));
        camera_ctx.ppa_handle = NULL;
        return -1;
    }

    camera_ctx.ppa_out_size = omv_esp32_camera_align_up(out_size, cache_align);
    camera_ctx.ppa_out_buf = heap_caps_aligned_calloc(
        cache_align, 1, camera_ctx.ppa_out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (camera_ctx.ppa_out_buf == NULL) {
        camera_ctx.ppa_out_buf = heap_caps_aligned_calloc(
            cache_align, 1, camera_ctx.ppa_out_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (camera_ctx.ppa_out_buf == NULL) {
        OMV_DEBUG("[OMV] camera ppa buffer alloc failed\r\n");
        omv_esp32_camera_release_ppa();
        return -1;
    }
    camera_ctx.active_input_width = OMV_ESP32_CAMERA_ACTIVE_INPUT_WIDTH;
    camera_ctx.active_input_height = OMV_ESP32_CAMERA_ACTIVE_INPUT_HEIGHT;
    camera_ctx.active_input_offset_x = OMV_ESP32_CAMERA_ACTIVE_INPUT_OFFSET_X;
    camera_ctx.active_input_offset_y = OMV_ESP32_CAMERA_ACTIVE_INPUT_OFFSET_Y;

    if ((camera_ctx.active_input_offset_x + camera_ctx.active_input_width) > camera_ctx.input_width) {
        camera_ctx.active_input_offset_x = 0;
    }
    if ((camera_ctx.active_input_offset_y + camera_ctx.active_input_height) > camera_ctx.input_height) {
        camera_ctx.active_input_offset_y = 0;
    }

    return 0;
}

static int omv_esp32_camera_init_buffers(void) {
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(req));
    req.count = OMV_ESP32_CAMERA_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(camera_ctx.fd, VIDIOC_REQBUFS, &req) != 0) {
        OMV_DEBUG("[OMV] camera reqbufs failed\r\n");
        return -1;
    }

    for (uint32_t i = 0; i < req.count && i < OMV_ESP32_CAMERA_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(camera_ctx.fd, VIDIOC_QUERYBUF, &buf) != 0) {
            OMV_DEBUG("[OMV] camera querybuf %lu failed\r\n", (unsigned long) i);
            return -1;
        }

        camera_ctx.buffers[i].ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                         camera_ctx.fd, buf.m.offset);
        camera_ctx.buffers[i].len = buf.length;

        if (camera_ctx.buffers[i].ptr == MAP_FAILED || camera_ctx.buffers[i].ptr == NULL) {
            camera_ctx.buffers[i].ptr = NULL;
            camera_ctx.buffers[i].len = 0;
            OMV_DEBUG("[OMV] camera mmap %lu failed\r\n", (unsigned long) i);
            return -1;
        }

        if (ioctl(camera_ctx.fd, VIDIOC_QBUF, &buf) != 0) {
            OMV_DEBUG("[OMV] camera qbuf %lu failed\r\n", (unsigned long) i);
            return -1;
        }
    }

    return 0;
}

static int omv_esp32_camera_start_stream(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(camera_ctx.fd, VIDIOC_STREAMON, &type) != 0) {
        OMV_DEBUG("[OMV] camera streamon failed\r\n");
        return -1;
    }

    camera_ctx.streaming = true;
    return 0;
}

void omv_esp32_camera_init0(void) {
    memset(&camera_ctx, 0, sizeof(camera_ctx));
    camera_ctx.fd = -1;
    mutex_init0(&camera_ctx.lock);
}

int omv_esp32_camera_init(void) {
    if (camera_ctx.initialized) {
        return 0;
    }

    static const esp_video_init_csi_config_t csi_config[] = {
        {
            .sccb_config = {
                .init_sccb = true,
                .i2c_config = {
                    .port = OMV_ESP32_CAMERA_SCCB_I2C_PORT,
                    .scl_pin = OMV_ESP32_CAMERA_SCCB_I2C_SCL_PIN,
                    .sda_pin = OMV_ESP32_CAMERA_SCCB_I2C_SDA_PIN,
                },
                .freq = OMV_ESP32_CAMERA_SCCB_I2C_FREQ,
            },
            .reset_pin = OMV_ESP32_CAMERA_SENSOR_RESET_PIN,
            .pwdn_pin = OMV_ESP32_CAMERA_SENSOR_PWDN_PIN,
            .dont_init_ldo = false,
        },
    };
    const esp_video_init_config_t video_config = {
        .csi = csi_config,
    };

    if (!omv_esp32_camera_start_xclk()) {
        return -1;
    }

    esp_err_t ret = esp_video_init(&video_config);
    if (ret != ESP_OK) {
        OMV_DEBUG("[OMV] camera esp_video_init failed: %s\r\n", esp_err_to_name(ret));
        omv_esp32_camera_stop_xclk();
        return -1;
    }
    camera_ctx.video_inited = true;

    if (omv_esp32_camera_open_device() != 0 ||
        omv_esp32_camera_set_format() != 0 ||
        omv_esp32_camera_init_ppa() != 0 ||
        omv_esp32_camera_init_buffers() != 0 ||
        omv_esp32_camera_start_stream() != 0) {
        omv_esp32_camera_deinit();
        return -1;
    }

    camera_ctx.initialized = true;
    return 0;
}

void omv_esp32_camera_deinit(void) {
    if (camera_ctx.streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(camera_ctx.fd, VIDIOC_STREAMOFF, &type);
        camera_ctx.streaming = false;
    }

    omv_esp32_camera_release_buffers();
    omv_esp32_camera_release_ppa();

    if (camera_ctx.fd >= 0) {
        close(camera_ctx.fd);
        camera_ctx.fd = -1;
    }

    if (camera_ctx.video_inited) {
        esp_video_deinit();
    }

    omv_esp32_camera_stop_xclk();

    camera_ctx.initialized = false;
    camera_ctx.video_inited = false;
    camera_ctx.hmirror = false;
    camera_ctx.vflip = false;
    camera_ctx.input_width = 0;
    camera_ctx.input_height = 0;
    camera_ctx.active_input_width = 0;
    camera_ctx.active_input_height = 0;
    camera_ctx.active_input_offset_x = 0;
    camera_ctx.active_input_offset_y = 0;
    camera_ctx.width = 0;
    camera_ctx.height = 0;
    camera_ctx.pixfmt = 0;
    camera_ctx.output_pixfmt = PIXFORMAT_RGB565;
}

bool omv_esp32_camera_is_ready(void) {
    return camera_ctx.initialized && camera_ctx.streaming &&
           camera_ctx.ppa_handle != NULL &&
           camera_ctx.ppa_out_buf != NULL &&
           camera_ctx.pixfmt == V4L2_PIX_FMT_RGB565 &&
           camera_ctx.width != 0 && camera_ctx.height != 0;
}

bool omv_esp32_camera_set_pixformat(uint32_t pixformat) {
    if ((pixformat != PIXFORMAT_RGB565) && (pixformat != PIXFORMAT_GRAYSCALE)) {
        return false;
    }

    mutex_lock(&camera_ctx.lock, MUTEX_TID_OMV);
    if (camera_ctx.output_pixfmt == pixformat) {
        mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
        return true;
    }

    camera_ctx.output_pixfmt = pixformat;
    if (camera_ctx.initialized && !omv_esp32_camera_reinit_ppa_locked()) {
        mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
        return false;
    }
    mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
    return true;
}

uint32_t omv_esp32_camera_get_pixformat(void) {
    return camera_ctx.output_pixfmt;
}

bool omv_esp32_camera_set_framesize(uint32_t framesize) {
    uint32_t width = 0;
    uint32_t height = 0;

    if (!omv_esp32_camera_framesize_to_dimensions(framesize, &width, &height)) {
        return false;
    }

    mutex_lock(&camera_ctx.lock, MUTEX_TID_OMV);
    if ((camera_ctx.width == width) && (camera_ctx.height == height)) {
        mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
        return true;
    }

    camera_ctx.width = width;
    camera_ctx.height = height;
    if (camera_ctx.initialized && !omv_esp32_camera_reinit_ppa_locked()) {
        mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
        return false;
    }
    mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
    return true;
}

uint32_t omv_esp32_camera_get_width(void) {
    return camera_ctx.width;
}

uint32_t omv_esp32_camera_get_height(void) {
    return camera_ctx.height;
}

uint32_t omv_esp32_camera_get_id(void) {
    return OMV_ESP32_CAMERA_SENSOR_ID;
}

bool omv_esp32_camera_set_hmirror(bool enable) {
    if (!omv_esp32_camera_is_ready()) {
        return false;
    }

    mutex_lock(&camera_ctx.lock, MUTEX_TID_OMV);
    camera_ctx.hmirror = enable;
    mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
    return true;
}

bool omv_esp32_camera_get_hmirror(void) {
    return camera_ctx.hmirror;
}

bool omv_esp32_camera_set_vflip(bool enable) {
    if (!omv_esp32_camera_is_ready()) {
        return false;
    }

    mutex_lock(&camera_ctx.lock, MUTEX_TID_OMV);
    camera_ctx.vflip = enable;
    mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
    return true;
}

bool omv_esp32_camera_get_vflip(void) {
    return camera_ctx.vflip;
}

bool omv_esp32_camera_capture(uint8_t *pixels, size_t size) {
    struct v4l2_buffer buf;
    size_t expected_size;
    bool ok = false;
    uint32_t output_pixfmt;

    if (!omv_esp32_camera_is_ready() || pixels == NULL) {
        return false;
    }

    expected_size = (size_t) camera_ctx.width * (size_t) camera_ctx.height *
                    omv_esp32_camera_output_bpp(camera_ctx.output_pixfmt);
    if (size < expected_size) {
        return false;
    }

    mutex_lock(&camera_ctx.lock, MUTEX_TID_OMV);
    output_pixfmt = camera_ctx.output_pixfmt;

    if (!omv_esp32_camera_dequeue_buffer(&buf)) {
        goto exit;
    }

    ppa_srm_oper_config_t ppa_cfg = {
        .in.buffer = camera_ctx.buffers[buf.index].ptr,
        .in.pic_w = camera_ctx.input_width,
        .in.pic_h = camera_ctx.input_height,
        .in.block_w = camera_ctx.active_input_width,
        .in.block_h = camera_ctx.active_input_height,
        .in.block_offset_x = camera_ctx.active_input_offset_x,
        .in.block_offset_y = camera_ctx.active_input_offset_y,
        .in.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        .out.buffer = camera_ctx.ppa_out_buf,
        .out.buffer_size = camera_ctx.ppa_out_size,
        .out.pic_w = camera_ctx.width,
        .out.pic_h = camera_ctx.height,
        .out.block_offset_x = 0,
        .out.block_offset_y = 0,
        .out.srm_cm = omv_esp32_camera_output_color_mode(output_pixfmt),
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float) camera_ctx.width / (float) camera_ctx.active_input_width,
        .scale_y = (float) camera_ctx.height / (float) camera_ctx.active_input_height,
        .mirror_x = camera_ctx.hmirror,
        .mirror_y = camera_ctx.vflip,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_cache_msync(camera_ctx.ppa_out_buf, camera_ctx.ppa_out_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);

    if (ppa_do_scale_rotate_mirror(camera_ctx.ppa_handle, &ppa_cfg) != ESP_OK) {
        omv_esp32_camera_queue_buffer(&buf);
        goto exit;
    }

    esp_cache_msync(camera_ctx.ppa_out_buf, camera_ctx.ppa_out_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    memcpy(pixels, camera_ctx.ppa_out_buf, expected_size);

    if (!omv_esp32_camera_queue_buffer(&buf)) {
        goto exit;
    }

    ok = true;

exit:
    mutex_unlock(&camera_ctx.lock, MUTEX_TID_OMV);
    return ok;
}
