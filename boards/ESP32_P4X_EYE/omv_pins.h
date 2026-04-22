#ifndef __OMV_PINS_H__
#define __OMV_PINS_H__

// Camera pins.
#define OMV_ESP32_CAMERA_SCCB_I2C_PORT          (0)
#define OMV_ESP32_CAMERA_SCCB_I2C_SCL_PIN       (13)
#define OMV_ESP32_CAMERA_SCCB_I2C_SDA_PIN       (14)
#define OMV_ESP32_CAMERA_SENSOR_RESET_PIN       (26)
#define OMV_ESP32_CAMERA_SENSOR_PWDN_PIN        (12)
#define OMV_ESP32_CAMERA_XCLK_PIN               (11)

// LCD pins.
#define OMV_ESP32_LCD_SPI_HOST                  (2)
#define OMV_ESP32_LCD_PIN_MOSI                  (16)
#define OMV_ESP32_LCD_PIN_CLK                   (17)
#define OMV_ESP32_LCD_PIN_CS                    (18)
#define OMV_ESP32_LCD_PIN_DC                    (19)
#define OMV_ESP32_LCD_PIN_RST                   (15)
#define OMV_ESP32_LCD_PIN_BL                    (20)

// SD card pins.
#define OMV_ESP32_SD_EN_PIN                     (46)
#define OMV_ESP32_SD_DETECT_PIN                 (45)
#define OMV_ESP32_SD_LDO_CHAN_ID                (4)

#endif // __OMV_PINS_H__
