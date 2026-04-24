#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"

#include "omv_board.h"
#include "omv_boardconfig.h"
#include "omv_debug.h"

int omv_esp32_board_camera_start_xclk(esp_cam_sensor_xclk_handle_t *xclk_handle) {
    if (xclk_handle != NULL) {
        *xclk_handle = NULL;
    }
    return 0;
}

void omv_esp32_board_camera_stop_xclk(esp_cam_sensor_xclk_handle_t xclk_handle) {
    (void) xclk_handle;
}

int omv_esp32_board_camera_video_init(void) {
    const esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = true,
            .i2c_config = {
                .port = OMV_ESP32_I2C_PORT,
                .scl_pin = OMV_ESP32_I2C_SCL_PIN,
                .sda_pin = OMV_ESP32_I2C_SDA_PIN,
            },
            .freq = OMV_ESP32_CAMERA_SCCB_I2C_FREQ,
        },
        .reset_pin = OMV_ESP32_CAMERA_SENSOR_RESET_PIN,
        .pwdn_pin = OMV_ESP32_CAMERA_SENSOR_PWDN_PIN,
    };
    const esp_video_init_config_t video_config = {
        .csi = &csi_config,
    };

    esp_err_t ret = esp_video_init(&video_config);
    if (ret != ESP_OK) {
        OMV_DEBUG("[OMV] camera esp_video_init failed: %s\r\n", esp_err_to_name(ret));
        return -1;
    }

    return 0;
}

void omv_esp32_board_camera_video_deinit(void) {
    esp_video_deinit();
}

int omv_esp32_board_camera_open_device(int *fd) {
    struct v4l2_capability capability = {0};

    if (fd == NULL) {
        return -1;
    }

    *fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
    if (*fd < 0) {
        OMV_DEBUG("[OMV] camera open %s failed\r\n", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return -1;
    }

    if (ioctl(*fd, VIDIOC_QUERYCAP, &capability) != 0) {
        OMV_DEBUG("[OMV] camera querycap failed\r\n");
        close(*fd);
        *fd = -1;
        return -1;
    }

    return 0;
}

int omv_esp32_board_camera_set_format(int fd, uint32_t *input_width, uint32_t *input_height, uint32_t *pixfmt) {
    struct v4l2_format format;

    if ((input_width == NULL) || (input_height == NULL) || (pixfmt == NULL)) {
        return -1;
    }

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = OMV_ESP32_CAMERA_INPUT_WIDTH;
    format.fmt.pix.height = OMV_ESP32_CAMERA_INPUT_HEIGHT;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;

    if (ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
        OMV_DEBUG("[OMV] camera set fmt failed\r\n");
        return -1;
    }

    *input_width = format.fmt.pix.width;
    *input_height = format.fmt.pix.height;
    *pixfmt = format.fmt.pix.pixelformat;
    return 0;
}

uint32_t omv_esp32_board_camera_get_sensor_id(void) {
    return OMV_ESP32_CAMERA_SENSOR_ID;
}
