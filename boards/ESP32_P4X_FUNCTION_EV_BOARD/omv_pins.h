#ifndef __OMV_PINS_H__
#define __OMV_PINS_H__

// Shared I2C bus used by the camera SCCB path.
#define OMV_ESP32_I2C_PORT                         (1)
#define OMV_ESP32_I2C_SCL_PIN                      (8)
#define OMV_ESP32_I2C_SDA_PIN                      (7)
#define OMV_ESP32_I2C_FREQ                         (400000)

// The SC2336 camera module provides its own 24 MHz clock.
#define OMV_ESP32_CAMERA_XCLK_PIN                  (-1)
#define OMV_ESP32_CAMERA_SENSOR_RESET_PIN          (-1)
#define OMV_ESP32_CAMERA_SENSOR_PWDN_PIN           (-1)

// SDMMC slot 0 uses IO MUX pins on ESP32-P4 Function EV Board.
#define OMV_ESP32_SDMMC_SLOT                       (0)
#define OMV_ESP32_SDMMC_WIDTH                      (4)
#define OMV_ESP32_SD_LDO_CHAN_ID                   (4)

// EK79007 MIPI-DSI LCD.
#define OMV_ESP32_LCD_SPI_HOST                     (2)
#define OMV_ESP32_LCD_PIN_BL                       (26)
#define OMV_ESP32_LCD_PIN_RST                      (27)

#endif // __OMV_PINS_H__
