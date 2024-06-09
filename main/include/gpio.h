#ifndef _GPIO_H_
#define _GPIO_H_
#include <stdint.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
// #include <freertos/semphr.h>

void gpio_init(uint8_t *pins,uint8_t NumofPin,uint8_t gpio_mode);

void gpio_intr_init(uint8_t pin,gpio_int_type_t intr_type,gpio_isr_t isr_handler);

void gpio_boot_mcu(uint8_t boot,uint8_t rst);

void gpio_run_mcu(uint8_t boot,uint8_t rst );
#endif