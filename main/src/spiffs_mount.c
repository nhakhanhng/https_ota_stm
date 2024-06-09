#include "spiffs_mount.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "sys_confg.h"

#define TAG "SPIFFS_FILE"

esp_err_t mount_file(const char *base_path)
{
#if DEBUG
    ESP_LOGI(TAG, "Initializing SPIFFS");
#endif
    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = NULL,
        .max_files = 3, // This sets the maximum number of files that can be open at the same time
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
#if DEBUG
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
#endif
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
#if DEBUG
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
#endif
        }
        else
        {
#if DEBUG
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
#endif
        }
        return ret;
    }
    return ESP_OK;
    //     ret = esp_spiffs_info(NULL, &Para_system_data->total, &Para_system_data->used);
    //     if (ret != ESP_OK)
    //     {
    // #if DEBUG
    //         ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    // #endif
    //         return ret;
    //     }
    // #if DEBUG
    //     ESP_LOGI(TAG, "Partition size: total: %d, used: %d", Para_system_data->total, Para_system_data->used);
    // #endif
    //     return ESP_OK;
}

esp_err_t write_bin_file(char *path, char *bin, int file_size)
{
    struct stat stat_file;
    if (stat(path, &stat_file) == 0)
    {
        // Delete it if it exists
        ESP_LOGI(TAG, "File existed");
        unlink(path);
    }
    else
    {
        ESP_LOGE(TAG, "Error to load file");
        return ESP_FAIL;
    }
    FILE *fptr = fopen(path, "w+");
    if (fptr == NULL)
    {
#if DEBUG
        ESP_LOGE(TAG, "Failed to open file");
#endif
        fclose(fptr);
        return ESP_FAIL;
    }
    if (file_size != fwrite(bin, 1, file_size, fptr))
    {
#if DEBUG
        ESP_LOGE(TAG, "Fail to write file");
#endif
        fclose(fptr);
        return ESP_FAIL;
    }
    fclose(fptr);
    return ESP_OK;
}

uint8_t check_firmware_exist()
{
    FILE *file = fopen("/bin_file/firmware.bin", "r");
    if (file == NULL)
    {
#if DEBUG
        ESP_LOGI(TAG, "Firmware does not exist");
#endif
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

esp_err_t read_bin_file_buffer(char *file_path, char *buffer, int start_index, int *buffer_size)
{
    struct stat entry_stat;
    if (stat(file_path, &entry_stat) == -1)
    {
#if DEBUG
        ESP_LOGE(TAG, "Failed to find firmware");
#endif
        // fclose(file);
        // return ESP_FAIL;
    }
    FILE *fptr = fopen(file_path, "r+");
    if (fptr == NULL)
    {
#if DEBUG
        ESP_LOGE(TAG,"Failed to create file: %s",file_path);
#endif
    }
    // fseek(fptr,start_index,SEEK_SET);
    int read_buffer_size = entry_stat.st_size - start_index;
    printf("Buffer size: %d, %d\r\n", read_buffer_size, *buffer_size);
    // *buffer_size = (*buffer_size > read_buffer_size)? read_buffer_size : *buffer_size;
    if (*buffer_size > read_buffer_size)
    {
        *buffer_size = read_buffer_size;
    }
    read_buffer_size = fread(buffer, 1, *buffer_size, fptr);
    if (read_buffer_size != *buffer_size)
    {
#if DEBUG
        ESP_LOGE(TAG, "Fail to read firmware: %d", read_buffer_size);
#endif
        return ESP_FAIL;
    };
    fclose(fptr);
    return ESP_OK;
}