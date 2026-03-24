/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV ESP32 port abstraction layer.
 */
#ifndef __OMV_PORTCONFIG_H__
#define __OMV_PORTCONFIG_H__

#include <stdint.h>

#define OMV_GPIO_MODE_INPUT     (0)
#define OMV_GPIO_MODE_OUTPUT    (1)
#define OMV_GPIO_MODE_ALT       (2)

#define OMV_GPIO_PULL_NONE      (0)
#define OMV_GPIO_PULL_UP        (1)
#define OMV_GPIO_PULL_DOWN      (2)

#define OMV_GPIO_SPEED_LOW      (0)
#define OMV_GPIO_SPEED_HIGH     (1)

typedef int omv_gpio_t;
typedef int omv_i2c_dev_t;
typedef int omv_spi_dev_t;

#endif // __OMV_PORTCONFIG_H__
