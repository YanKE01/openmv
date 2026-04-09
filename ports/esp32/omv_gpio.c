/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV GPIO port for ESP32.
 */
#include "driver/gpio.h"

#include "omv_gpio.h"

void omv_gpio_init0(void) {
}

void omv_gpio_config(omv_gpio_t pin, uint32_t mode, uint32_t pull, uint32_t speed, uint32_t af) {
    (void) speed;
    (void) af;

    if (pin < 0 || pin >= GPIO_NUM_MAX) {
        return;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = (pull == OMV_GPIO_PULL_UP),
        .pull_down_en = (pull == OMV_GPIO_PULL_DOWN),
        .intr_type = GPIO_INTR_DISABLE,
    };

    switch (mode) {
        case OMV_GPIO_MODE_INPUT:
            cfg.mode = GPIO_MODE_INPUT;
            break;
        case OMV_GPIO_MODE_OUTPUT:
            cfg.mode = GPIO_MODE_OUTPUT;
            break;
#ifdef OMV_GPIO_MODE_OUTPUT_OD
        case OMV_GPIO_MODE_OUTPUT_OD:
            cfg.mode = GPIO_MODE_OUTPUT_OD;
            break;
#endif
        case OMV_GPIO_MODE_ALT:
            cfg.mode = GPIO_MODE_INPUT_OUTPUT;
            break;
#ifdef OMV_GPIO_MODE_ALT_OD
        case OMV_GPIO_MODE_ALT_OD:
            cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
            break;
#endif
        default:
            break;
    }

    gpio_config(&cfg);
}

void omv_gpio_deinit(omv_gpio_t pin) {
    if (pin < 0 || pin >= GPIO_NUM_MAX) {
        return;
    }

    gpio_reset_pin(pin);
}

bool omv_gpio_read(omv_gpio_t pin) {
    if (pin < 0 || pin >= GPIO_NUM_MAX) {
        return false;
    }

    return gpio_get_level(pin);
}

void omv_gpio_write(omv_gpio_t pin, bool value) {
    if (pin < 0 || pin >= GPIO_NUM_MAX) {
        return;
    }

    gpio_set_level(pin, value);
}

void omv_gpio_irq_register(omv_gpio_t pin, omv_gpio_callback_t callback, void *data) {
    (void) pin;
    (void) callback;
    (void) data;
}

void omv_gpio_irq_enable(omv_gpio_t pin, bool enable) {
    (void) pin;
    (void) enable;
}

void omv_gpio_clock_enable(omv_gpio_t pin, bool enable) {
    (void) pin;
    (void) enable;
}
