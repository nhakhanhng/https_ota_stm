#include "gpio.h"
#include <driver/gpio.h>
#include <freertos/task.h>
#include "freertos/FreeRTOS.h"
#include <freertos/semphr.h>
#include <stdint.h>

void gpio_init(uint8_t *pins, uint8_t NumofPin, uint8_t gpio_mode)
{

    uint64_t pin_sel = 0UL;
    for (int i = 0; i < NumofPin; i++)
    {
        pin_sel |= (1UL << pins[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_sel,
        .mode = gpio_mode,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = 0,
        .pull_up_en = 0};
    gpio_config(&io_conf);
    
    // for (int i = 0;i < NumofPin;i++) {
    //     gpio_set_direction(pins[i],GPIO_MODE_OUTPUT);
    // }


    // for (int i = 0;i <NumofPin;i++) {
    //     gpio_set_level(pins[i],0);
    // }
}

void gpio_intr_init(uint8_t pin,gpio_int_type_t intr_type,gpio_isr_t isr_handler) {
    gpio_config_t isr_config = {
        .pin_bit_mask = (1UL << pin),
        .mode = GPIO_MODE_INPUT,
        .intr_type = intr_type
    };
    ESP_ERROR_CHECK(gpio_config(&isr_config));
    // ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin,isr_handler,(void *)pin));
}

void gpio_boot_mcu(uint8_t boot, uint8_t rst)
{
    gpio_set_level(boot, 1);
    gpio_set_level(rst, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(rst, 1);
    vTaskDelay(50 / portTICK_PERIOD_MS);
}

void gpio_run_mcu(uint8_t boot, uint8_t rst)
{
    gpio_set_level(boot, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(rst, 0);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    gpio_set_level(rst, 1);
}