/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV ESP32 camera integration skeleton.
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"
#include "py/mphal.h"

#include "omv_camera.h"

#define OMV_ESP32_CAMERA_WIDTH                  (640)
#define OMV_ESP32_CAMERA_HEIGHT                 (480)
#define OMV_ESP32_CAMERA_BUFFER_COUNT           (2)
#define OMV_ESP32_CAMERA_SCCB_I2C_PORT          (0)
#define OMV_ESP32_CAMERA_SCCB_I2C_SCL_PIN       (8)
#define OMV_ESP32_CAMERA_SCCB_I2C_SDA_PIN       (7)
#define OMV_ESP32_CAMERA_SCCB_I2C_FREQ          (100000)
#define OMV_ESP32_CAMERA_SENSOR_RESET_PIN       (-1)
#define OMV_ESP32_CAMERA_SENSOR_PWDN_PIN        (-1)

typedef struct {
    void *ptr;
    size_t len;
} omv_esp32_camera_buffer_t;

typedef struct {
    bool initialized;
    bool video_inited;
    bool streaming;
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t pixfmt;
    omv_esp32_camera_buffer_t buffers[OMV_ESP32_CAMERA_BUFFER_COUNT];
} omv_esp32_camera_t;

static omv_esp32_camera_t camera_ctx;

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
    struct v4l2_capability capability;

    camera_ctx.fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (camera_ctx.fd < 0) {
        mp_printf(&mp_plat_print, "ESP32 camera open failed\r\n");
        return -1;
    }

    if (ioctl(camera_ctx.fd, VIDIOC_QUERYCAP, &capability) != 0) {
        mp_printf(&mp_plat_print, "ESP32 camera querycap failed\r\n");
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
    format.fmt.pix.width = OMV_ESP32_CAMERA_WIDTH;
    format.fmt.pix.height = OMV_ESP32_CAMERA_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;

    if (ioctl(camera_ctx.fd, VIDIOC_S_FMT, &format) != 0) {
        mp_printf(&mp_plat_print, "ESP32 camera set fmt failed\r\n");
        return -1;
    }

    camera_ctx.width = format.fmt.pix.width;
    camera_ctx.height = format.fmt.pix.height;
    camera_ctx.pixfmt = format.fmt.pix.pixelformat;

    mp_printf(&mp_plat_print,
              "ESP32 camera fmt %lux%lu fourcc=0x%08lx\r\n",
              (unsigned long) camera_ctx.width,
              (unsigned long) camera_ctx.height,
              (unsigned long) camera_ctx.pixfmt);
    return 0;
}

static int omv_esp32_camera_init_buffers(void) {
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(req));
    req.count = OMV_ESP32_CAMERA_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(camera_ctx.fd, VIDIOC_REQBUFS, &req) != 0) {
        mp_printf(&mp_plat_print, "ESP32 camera reqbufs failed\r\n");
        return -1;
    }

    for (uint32_t i = 0; i < req.count && i < OMV_ESP32_CAMERA_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(camera_ctx.fd, VIDIOC_QUERYBUF, &buf) != 0) {
            mp_printf(&mp_plat_print, "ESP32 camera querybuf %lu failed\r\n", (unsigned long) i);
            return -1;
        }

        camera_ctx.buffers[i].ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                         camera_ctx.fd, buf.m.offset);
        camera_ctx.buffers[i].len = buf.length;

        if (camera_ctx.buffers[i].ptr == MAP_FAILED || camera_ctx.buffers[i].ptr == NULL) {
            camera_ctx.buffers[i].ptr = NULL;
            camera_ctx.buffers[i].len = 0;
            mp_printf(&mp_plat_print, "ESP32 camera mmap %lu failed\r\n", (unsigned long) i);
            return -1;
        }

        if (ioctl(camera_ctx.fd, VIDIOC_QBUF, &buf) != 0) {
            mp_printf(&mp_plat_print, "ESP32 camera qbuf %lu failed\r\n", (unsigned long) i);
            return -1;
        }
    }

    return 0;
}

static int omv_esp32_camera_start_stream(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(camera_ctx.fd, VIDIOC_STREAMON, &type) != 0) {
        mp_printf(&mp_plat_print, "ESP32 camera streamon failed\r\n");
        return -1;
    }

    camera_ctx.streaming = true;
    return 0;
}

void omv_esp32_camera_init0(void) {
    memset(&camera_ctx, 0, sizeof(camera_ctx));
    camera_ctx.fd = -1;
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

    esp_err_t ret = esp_video_init(&video_config);
    if (ret != ESP_OK) {
        mp_printf(&mp_plat_print, "ESP32 camera init failed: %s\r\n", esp_err_to_name(ret));
        return -1;
    }
    camera_ctx.video_inited = true;

    if (omv_esp32_camera_open_device() != 0 ||
        omv_esp32_camera_set_format() != 0 ||
        omv_esp32_camera_init_buffers() != 0 ||
        omv_esp32_camera_start_stream() != 0) {
        omv_esp32_camera_deinit();
        return -1;
    }

    camera_ctx.initialized = true;
    mp_printf(&mp_plat_print, "ESP32 camera init done\r\n");
    return 0;
}

void omv_esp32_camera_deinit(void) {
    if (camera_ctx.streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(camera_ctx.fd, VIDIOC_STREAMOFF, &type);
        camera_ctx.streaming = false;
    }

    omv_esp32_camera_release_buffers();

    if (camera_ctx.fd >= 0) {
        close(camera_ctx.fd);
        camera_ctx.fd = -1;
    }

    if (camera_ctx.video_inited) {
        esp_video_deinit();
    }

    camera_ctx.initialized = false;
    camera_ctx.video_inited = false;
    camera_ctx.width = 0;
    camera_ctx.height = 0;
    camera_ctx.pixfmt = 0;
}

bool omv_esp32_camera_is_ready(void) {
    return camera_ctx.initialized && camera_ctx.streaming &&
           camera_ctx.pixfmt == V4L2_PIX_FMT_RGB565 &&
           camera_ctx.width != 0 && camera_ctx.height != 0;
}

uint32_t omv_esp32_camera_get_width(void) {
    return camera_ctx.width;
}

uint32_t omv_esp32_camera_get_height(void) {
    return camera_ctx.height;
}

bool omv_esp32_camera_capture_rgb565(uint16_t *pixels, size_t pixel_count) {
    struct v4l2_buffer buf;
    size_t expected_size;
    size_t copy_size;

    if (!omv_esp32_camera_is_ready() || pixels == NULL) {
        return false;
    }

    expected_size = (size_t) camera_ctx.width * (size_t) camera_ctx.height * sizeof(uint16_t);
    if ((pixel_count * sizeof(uint16_t)) < expected_size) {
        return false;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(camera_ctx.fd, VIDIOC_DQBUF, &buf) != 0) {
        return false;
    }

    if (buf.index >= OMV_ESP32_CAMERA_BUFFER_COUNT ||
        camera_ctx.buffers[buf.index].ptr == NULL ||
        (buf.flags & V4L2_BUF_FLAG_ERROR)) {
        if (buf.index < OMV_ESP32_CAMERA_BUFFER_COUNT) {
            ioctl(camera_ctx.fd, VIDIOC_QBUF, &buf);
        }
        return false;
    }

    copy_size = buf.bytesused;
    if (copy_size > expected_size) {
        copy_size = expected_size;
    }
    memcpy(pixels, camera_ctx.buffers[buf.index].ptr, copy_size);

    if (copy_size < expected_size) {
        memset(((uint8_t *) pixels) + copy_size, 0, expected_size - copy_size);
    }

    if (ioctl(camera_ctx.fd, VIDIOC_QBUF, &buf) != 0) {
        return false;
    }

    return true;
}
