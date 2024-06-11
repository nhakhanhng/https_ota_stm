#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sleep.h"
#include <driver/rtc_io.h>
#include <driver/gpio.h>

#include "drive_ota.h"
#include "sys_confg.h"
#include "esp_event.h"
#include "uart.h"
#include "driver/i2c.h"
#include "ssd1306.h"
#include "wifi_connect.h"
#include "gpio.h"
#include <esp_timer.h>
#include <esp_systick_etm.h>

#define TAG "ESP-OTA"

EventGroupHandle_t ButtonEventGr;

ESP_EVENT_DEFINE_BASE(OTA_EVENTS);
drive_ota_handle_t drive_ota = {};

extern const uint8_t server_cert_pem_start[] asm("_binary_google_cer_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_google_cer_end");

void ota_event_loop_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    image_version_t *drive_version;
    char display_buffer[80] = "";
    switch (event_id)
    {
        // case OTA_EVENT_GET_VERSION_FAIL:
        //     ESP_LOGI(TAG, "Failed to get version");
        //     task_ssd1306_display_clear();
        //     task_ssd1306_display_text("Failed to get version\nPlease check connection/permission");
        break;
    case OTA_EVENT_NEW_VERSION:
        /* code */
        ESP_LOGI(TAG, "New version event");
        drive_version = (image_version_t *)event_data;
        sprintf(display_buffer, "Current:%d.%d\nReject:%d.%d\nDrive:%d.%d\n,Update?", drive_ota.current_version.major_version, drive_ota.current_version.minor_version, drive_ota.reject_version.major_version, drive_ota.reject_version.minor_version, drive_version->major_version, drive_version->minor_version);
        task_ssd1306_display_clear();
        task_ssd1306_display_text(display_buffer);
        rtc_gpio_set_level(GPIO_NUM_2, 1);
        break;
    case OTA_EVENT_OLD_VERSION:
        /* code */
        ESP_LOGI(TAG, "Old version event");
        drive_version = (image_version_t *)event_data;
        sprintf(display_buffer, "Current:%d.%d\nReject:%d.%d\nDrive:%d.%d\n", drive_ota.current_version.major_version, drive_ota.current_version.minor_version, drive_ota.reject_version.major_version, drive_ota.reject_version.minor_version, drive_version->major_version, drive_version->minor_version);
        task_ssd1306_display_clear();
        task_ssd1306_display_text(display_buffer);
        break;
    case OTA_EVENT_FLASH_FIRMWARE:
        ESP_LOGI(TAG, "Flashing image");
        task_ssd1306_display_clear();
        task_ssd1306_display_text("Flashing new version ...");
        break;
    case OTA_EVENT_GET_FIRMWARE_FAIL:
        ESP_LOGE(TAG, "Failed to get new image");
        task_ssd1306_display_clear();
        task_ssd1306_display_text("Failed to get version\nPlease check connection/permission");
        break;
    case OTA_EVENT_CONNECT_WIFI_FAIL:
        wifi_infor_t *wifi = (wifi_infor_t *)event_data;
        ESP_LOGE(TAG, "Failed to connect wifi");
        task_ssd1306_display_clear();
        sprintf(display_buffer, "Please check wifi info\nSSID:%s\nPass:%s", wifi->ssid, wifi->password);
        task_ssd1306_display_text(display_buffer);
        break;
    case OTA_EVENT_FLASH_FIRMWARE_SUCCESS:
        ESP_LOGI(TAG, "Success to flash new firmware");
        task_ssd1306_display_clear();
        task_ssd1306_display_text("Success to flash new version to MCU");
        break;
    case OTA_EVENT_FLASH_FIRMWARE_FAIL:
        ESP_LOGI(TAG, "Success to flash new firmware");
        task_ssd1306_display_clear();
        task_ssd1306_display_text("Failed to flash firmware\nPlease check connection");
        break;
    default:
        break;
    }
}

void IRAM_ATTR gpio_intr_handler(void *arg)
{
    // SemaphoreHandle_t semaphr = (SemaphoreHandle_t)arg;
    // xSemaphoreGiveFromISR(semaphr, pdTRUE);
    uint8_t pin = (uint8_t)arg;
    if (pin == ACCEPT_BTN)
    {
        xEventGroupSetBits(ButtonEventGr, ACCEPT_BIT);
    }
    else if (pin == REJECT_BTN)
    {
        xEventGroupSetBits(ButtonEventGr, REJECT_BIT);
    }
}

void i2c_master_init()
{
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000};
    i2c_param_config(I2C_NUM_0, &i2c_config);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "OTA example app_main start");
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    i2c_master_init();

    ssd1306_init();
    task_ssd1306_display_clear();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, &ota_event_loop_handler, NULL));

    // ESP_ERROR_CHECK(example_connect());
    // esp_sleep_enable_timer_wakeup()
    esp_event_loop_args_t ota_loop_args = {
        .queue_size = 5,
        .task_name = "ota_loop_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 3072,
        .task_core_id = tskNO_AFFINITY,
    };
    // SemaphoreHandle_t Semaphr;
    // Semaphr = xSemaphoreCreateBinary();
    ButtonEventGr = xEventGroupCreate();
    // EventBits_t ButtonEventBits;
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    gpio_intr_init(ACCEPT_BTN, GPIO_INTR_NEGEDGE, gpio_intr_handler);
    gpio_intr_init(REJECT_BTN, GPIO_INTR_NEGEDGE, gpio_intr_handler);
    // xSemaphoreTake(Semaphr,portMAX_DELAY);
    esp_event_loop_handle_t ota_loop_handle;
    esp_event_loop_create(&ota_loop_args, &ota_loop_handle);
    esp_event_handler_instance_register_with(ota_loop_handle, OTA_EVENTS, ESP_EVENT_ANY_ID, &ota_event_loop_handler, NULL, NULL);

    rtc_gpio_deinit(GPIO_NUM_0);
    rtc_gpio_pullup_en(GPIO_NUM_0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    rtc_gpio_init(GPIO_NUM_2);
    rtc_gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    rtc_gpio_set_level(GPIO_NUM_2, 0);
    // esp_deep_sleep_start();
    // drive_ota.url = DRIVE_URL;
    drive_ota.ButtonEventGr = ButtonEventGr;
    memcpy(drive_ota.url, DRIVE_URL, sizeof(DRIVE_URL));
    drive_ota.cert_pem = (char *)server_cert_pem_start;
    drive_ota.timeout_ms = 3000;
    drive_ota.max_authorization_retries = 5;
    // drive_ota.base_path = "/spiffs_dir";
    memcpy(drive_ota.base_path, BASE_PATH, sizeof(BASE_PATH));
    // drive_ota.file_bin_path = "/spiffs_dir/version.txt";
    memcpy(drive_ota.file_version_path, VERSION_PATH, sizeof(VERSION_PATH));
    memcpy(drive_ota.file_rej_version_path, REJ_VERSION_PATH, sizeof(REJ_VERSION_PATH));
    drive_ota.uart_num = UART;
    // drive_ota.wifi.ssid = WIFI_SSID;
    memcpy(drive_ota.wifi.ssid, WIFI_SSID, sizeof(WIFI_SSID));
    // drive_ota.wifi.password = WIFI_PASS;
    memcpy(drive_ota.wifi.password, WIFI_PASS, sizeof(WIFI_PASS));
    drive_ota.NumofDev = 1;
    drive_ota.BootPin = pvPortMalloc(1);
    drive_ota.RstPin = pvPortMalloc(1);
    *(drive_ota.BootPin) = BOOT_PIN;
    *(drive_ota.RstPin) = RST_PIN;
    esp_netif_create_default_wifi_sta();
    err = wifi_init_sta(drive_ota.wifi.ssid, drive_ota.wifi.password);
    if (err != ESP_OK)
    {
        char display_buffer[80] = "";
        task_ssd1306_display_clear();
        sprintf(display_buffer, "Please check wifi info\nSSID:%s\nPass:%s", drive_ota.wifi.ssid, drive_ota.wifi.password);
        task_ssd1306_display_text(display_buffer);
        ESP_LOGI(TAG, "Sleep");
        esp_deep_sleep(SLEEP_TIME_S);
    }
    drive_ota_init(&drive_ota);
    drive_ota_start(&drive_ota,&ota_event_loop_handler);
    // esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    // ESP_LOGI(TAG, "Wake up cause: %d", wakeup_cause);
    // image_version_t drive_version = {0};
    // uart_init(drive_ota.uart_num);
    // while (1)
    // {

    //     if (err == ESP_FAIL)
    //     {
    //         esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_CONNECT_WIFI_FAIL, &drive_ota.wifi, sizeof(drive_ota.wifi), portMAX_DELAY);
    //         break;
    //     }
    //     err = drive_ota_get_new_image_version(&drive_ota, &drive_version);
    //     if (err != ESP_OK)
    //     {
    //         ESP_LOGI(TAG, "Failed to get version!");
    //         esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_GET_FIRMWARE_FAIL, NULL, 0, portMAX_DELAY);
    //         vTaskDelay(60000 / portTICK_PERIOD_MS);
    //         break;
    //     }
    //     printf("Reject version: %hhd.%hhd\r\n",drive_ota.reject_version.major_version,drive_ota.reject_version.minor_version);
    //     err = validate_image_version(&drive_ota, &drive_version);
    //     if (err != ESP_OK)
    //     {
    //         ESP_LOGI(TAG, "Old or rejected version!");
    //         esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_OLD_VERSION, &drive_version, sizeof(drive_version), portMAX_DELAY);
    //         if (wakeup_cause != ESP_SLEEP_WAKEUP_EXT0)
    //         {
    //             // skip version
    //             ESP_LOGI(TAG, "Skip old version: %d.%d", drive_version.major_version, drive_version.minor_version);
    //             break;
    //         }
    //     }
    //     esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_NEW_VERSION, &drive_version, sizeof(drive_version), portMAX_DELAY);
    //     xEventGroupClearBits(ButtonEventGr, ACCEPT_BIT | REJECT_BIT);
    //     ButtonEventBits = xEventGroupWaitBits(ButtonEventGr, ACCEPT_BIT | REJECT_BIT, pdFALSE, pdFALSE, 60000 / portTICK_PERIOD_MS);
    //     if (ButtonEventBits & REJECT_BIT)
    //     {
    //         printf("Reject version\r\n");
    //         drive_ota_set_reject_version(&drive_ota, &drive_version);
    //         task_ssd1306_display_clear();
    //         break;
    //     }
    //     else if (!(ButtonEventBits & ACCEPT_BIT))
    //     {
    //         // notify new version available
    //         rtc_gpio_set_level(2,0);
    //         break;
    //     }

    //     // ESP_ERROR_CHECK(drive_ota_update_image_version(&drive_ota, &drive_version));
    //     err = drive_ota_get_new_image(&drive_ota, &drive_version);
    //     if (err != ESP_OK)
    //     {
    //         // ESP_LOGE(TAG,"Failed to get new image");
    //         esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_GET_FIRMWARE_FAIL, &drive_ota.download_link, sizeof(drive_ota.download_link), portMAX_DELAY);
    //         break;
    //     }
    //     esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_FLASH_FIRMWARE, &drive_ota, sizeof(drive_ota), portMAX_DELAY);
    //     for (int i = 0; i < MAX_FLASH_RETRIES; i++)
    //     {
    //         err = drive_ota_flash_image(&drive_ota);
    //         // int64_t complete_time_ms = esp_timer_get_time()/1000UL - start_time_ms;
    //         if (err == ESP_OK)
    //         {
    //             break;
    //             // ESP_LOGI(TAG,"Flash image success: %lldms",complete_time_ms);
    //         }
    //         // else ESP_LOGE(TAG,"Flash image failed: %lldms",complete_time_ms);
    //         // vTaskDelay(500/portTICK_PERIOD_MS);
    //     }
    //     if (err != ESP_OK)
    //     {
    //         esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_FLASH_FIRMWARE_FAIL, &drive_ota, sizeof(drive_ota), portMAX_DELAY);
    //         vTaskDelay(60000 / portTICK_PERIOD_MS);
    //         break;
    //     }
    //     rtc_gpio_set_level(2,0);
    //     esp_event_post_to(ota_loop_handle, OTA_EVENTS, OTA_EVENT_FLASH_FIRMWARE_SUCCESS, &drive_ota, sizeof(drive_ota), portMAX_DELAY);
    //     vTaskDelay(5000/portTICK_PERIOD_MS);
    //     task_ssd1306_display_clear();
    //     break;
    // }
    ESP_LOGI(TAG, "Sleep");
    esp_deep_sleep(SLEEP_TIME_S);

    // return ESP_OK;
}