#ifndef _DRIVE_OTA_H_

#define _DRIVE_OTA_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"
#include "sys_confg.h"
#include "driver/gpio.h"

#define IMAGE_VERSION_SIZE              50

ESP_EVENT_DECLARE_BASE(OTA_EVENTS);

enum
{
    OTA_EVENT_GET_VERSION_FAIL,
    OTA_EVENT_NEW_VERSION,
    OTA_EVENT_OLD_VERSION,
    OTA_EVENT_FLASH_FIRMWARE,
    OTA_EVENT_FLASH_FIRMWARE_FAIL,
    OTA_EVENT_FLASH_FIRMWARE_SUCCESS,
    OTA_EVENT_GET_FIRMWARE_FAIL,
    OTA_EVENT_CONNECT_WIFI_FAIL,
};


typedef struct Date_t {
    uint8_t date;
    uint8_t month;
    uint8_t year;
} Date_t;

typedef struct Time_t {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} Time_t;

struct image_version
{
    uint8_t major_version;
    uint8_t minor_version;
    char id[40];
    Date_t date;
    Time_t time;
} ;

typedef struct image_version image_version_t;

typedef struct {
    image_version_t version;
    char *bin_data;
    size_t bin_data_size;
} ota_image_handle_t;

typedef struct {
    char ssid[30];
    char password[10];
} wifi_infor_t;

typedef struct {
    wifi_infor_t wifi;
    esp_http_client_handle_t client;
    // ota_image_handle_t *image;
    image_version_t current_version;
    image_version_t reject_version;
    char url[120];
    char download_link[110];
    char *cert_pem;
    int timeout_ms;
    int max_authorization_retries;
    char base_path[20];
    char file_version_path[50];
    char file_rej_version_path[50];
    char* bin_buffer;
    int32_t buffer_size;
    uint8_t NumofDev;
    uint8_t *BootPin;
    uint8_t *RstPin;
    uint8_t uart_num;
    EventGroupHandle_t ButtonEventGr;
    EventBits_t EventBits;
    char response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1];
} drive_ota_handle_t;

esp_err_t drive_ota_init(drive_ota_handle_t *);
esp_err_t drive_ota_get_new_image(drive_ota_handle_t *,image_version_t *);
esp_err_t validate_image_version(drive_ota_handle_t *,image_version_t *);
esp_err_t drive_ota_update_image_version(drive_ota_handle_t *,image_version_t *);
esp_err_t drive_ota_flash_image(drive_ota_handle_t *);
esp_err_t drive_ota_get_new_image_version(drive_ota_handle_t *,image_version_t*);
void drive_ota_set_reject_version(drive_ota_handle_t *,image_version_t *);
void drive_ota_start(drive_ota_handle_t *,esp_event_handler_t);
// void drive_ota_start(drive_ota_handle_t*);
// esp_err_t drive_ota_update_image(drive_ota_handle_t *, ota_image_handle_t *);

#endif