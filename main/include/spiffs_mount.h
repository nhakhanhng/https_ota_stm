#ifndef _SPIFFS_MOUNT_H_
#define _SPIFFS_MOUNT_H 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
// #include "esp_http_client.h"

esp_err_t mount_file(const char *base_path);

esp_err_t write_bin_file(char *path,char *bin,int file_size);

uint8_t check_firmware_exist();

esp_err_t read_bin_file_buffer(char *file_path,char *buffer,int start_index,int *buffer_size);

#endif