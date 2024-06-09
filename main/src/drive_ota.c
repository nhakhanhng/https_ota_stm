#include "drive_ota.h"
#include <string.h>
#include <string.h>
#include "spiffs_mount.h"
#include <sys/stat.h>
#include "esp_tls.h"
#include "sys_confg.h"
#include "stm32_ota.h"
#include "cJSON.h"
#include "driver/uart.h"
#include <esp_timer.h>

#include "gpio.h"

#define TAG "HTTPS_OTA"
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        // Clean the buffer in case of a new request
        if (output_len == 0 && evt->user_data)
        {
            // we are just starting to copy the output data into the use
            memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
        }
        memcpy(evt->user_data, evt->data, evt->data_len);
        // printf("Data: %s\r\n",(char *)evt->user_data);
        /*
         *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
         *  However, event handler can also be used in case chunked encoding is used.
         */
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // If user_data buffer is configured, copy the response into the buffer
            int copy_len = 0;
            if (evt->user_data)
            {
                // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                if (copy_len)
                {
                    memcpy(evt->user_data + output_len, evt->data, copy_len);
                    // printf("User data: %s\r\n", (char *)evt->data);
                }
            }
            else
            {
                int content_len = esp_http_client_get_content_length(evt->client);
                if (output_buffer == NULL)
                {
                    // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                    output_buffer = (char *)calloc(content_len + 1, sizeof(char));
                    output_len = 0;
                    if (output_buffer == NULL)
                    {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                }
                copy_len = MIN(evt->data_len, (content_len - output_len));
                if (copy_len)
                {
                    memcpy(output_buffer + output_len, evt->data, copy_len);
                }
            }
            output_len += copy_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
        break;
    }
    return ESP_OK;
}

static bool process_again(int status_code)
{
    switch (status_code)
    {
    case HttpStatus_MovedPermanently:
    case HttpStatus_Found:
    case HttpStatus_SeeOther:
    case HttpStatus_TemporaryRedirect:
    case HttpStatus_PermanentRedirect:
    case HttpStatus_Unauthorized:
        return true;
    default:
        return false;
    }
    return false;
}

static bool redirection_required(int status_code)
{
    switch (status_code)
    {
    case HttpStatus_MovedPermanently:
    case HttpStatus_Found:
    case HttpStatus_SeeOther:
    case HttpStatus_TemporaryRedirect:
    case HttpStatus_PermanentRedirect:
        return true;
    default:
        return false;
    }
    return false;
}

static esp_err_t read_image_file_version(drive_ota_handle_t *ota_drive)
{
    ESP_LOGI(TAG, "Read file version");
    int read_size = IMAGE_VERSION_SIZE;
    ota_drive->bin_buffer = (char *)pvPortMalloc(read_size);
    char rej_ver_buffer[50] = "";
    if (read_bin_file_buffer(ota_drive->file_version_path, ota_drive->bin_buffer, 0, &read_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Fail to read file");
        return ESP_FAIL;
    }
    printf("Version: %.*s\r\n", read_size, ota_drive->bin_buffer);
    if (read_bin_file_buffer(ota_drive->file_rej_version_path, rej_ver_buffer, 0, &read_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Fail to read file");
        return ESP_FAIL;
    }
    printf("Reject Version: %s\r\n", rej_ver_buffer);
    if (strcmp(rej_ver_buffer, "") == 0)
    {
        ota_drive->reject_version.major_version = 0;
        ota_drive->reject_version.minor_version = 0;
    }
    else
    {
        sscanf(rej_ver_buffer, "%hhd.%hhd,%s", &ota_drive->reject_version.major_version, &ota_drive->reject_version.minor_version, ota_drive->reject_version.id);
        printf("Reject version: %d.%d\r\n",ota_drive->reject_version.major_version,ota_drive->reject_version.minor_version);
    }
    // sprintf(version_data,"%s",);
    // printf("Version data: %s",version_data);
    sscanf(ota_drive->bin_buffer, "%hhd.%hhd,%s", &ota_drive->current_version.major_version, &ota_drive->current_version.minor_version, ota_drive->current_version.id);
    // printf()
    // cJSON_free(JSON_Data);
    vPortFree(ota_drive->bin_buffer);
    return ESP_OK;
}

static void parse_json(char *json_string, cJSON *JSON_data)
{
    // Parse the JSON string
    JSON_data = cJSON_Parse(json_string);
    if (JSON_data == NULL)
    {
        ESP_LOGE("JSON", "Error before");
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            ESP_LOGE("JSON", "Error before: %s", error_ptr);
        }
        return;
    }
}

esp_err_t http_handle_response_code(esp_http_client_handle_t client, int status_code)
{
    esp_err_t err;
    if (redirection_required(status_code))
    {
        err = esp_http_client_set_redirection(client);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "URL redirection Failed");
            return err;
        }
    }
    else if (status_code == HttpStatus_Unauthorized)
    {
        printf("Unauthorized\r\n");
        // if (client->max_authorization_retries == 0) {
        //     ESP_LOGE(TAG, "Reached max_authorization_retries (%d)", status_code);
        //     return ESP_FAIL;
        // }
        // client->max_authorization_retries--;
        esp_http_client_add_auth(client);
        return ESP_FAIL;
    }
    else if (status_code == HttpStatus_NotFound || status_code == HttpStatus_Forbidden)
    {
        ESP_LOGE(TAG, "File not found(%d)", status_code);
        return ESP_FAIL;
    }
    else if (status_code >= HttpStatus_BadRequest && status_code < HttpStatus_InternalError)
    {
        ESP_LOGE(TAG, "Client error (%d)", status_code);
        return ESP_FAIL;
    }
    else if (status_code >= HttpStatus_InternalError)
    {
        ESP_LOGE(TAG, "Server error (%d)", status_code);
        return ESP_FAIL;
    }
    // else if (status_code == HttpStatus_Found) {
    //     #if DEBUG
    //     ESP_LOGE(TAG,"Please open access, status code: %d ",status_code);
    //     #endif
    //     return ESP_FAIL;
    // }

    char upgrade_data_buf[256];
    // process_again() returns true only in case of redirection.
    if (process_again(status_code))
    {
        while (1)
        {
            printf("Process again: %d\r\n", status_code);
            /*
             *  In case of redirection, esp_http_client_read() is called
             *  to clear the response buffer of http_client.
             */
            int data_read = esp_http_client_read(client, upgrade_data_buf, sizeof(upgrade_data_buf));
            if (data_read <= 0)
            {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

esp_err_t drive_ota_init(drive_ota_handle_t *ota_drive)
{
    esp_http_client_config_t http_config = {
        .url = ota_drive->url,
        .cert_pem = (char *)ota_drive->cert_pem,
        .timeout_ms = ota_drive->timeout_ms,
        .keep_alive_enable = true,
        .max_authorization_retries = ota_drive->max_authorization_retries,
        .event_handler = _http_event_handler,
        .port = 443,
        .user_data = ota_drive->response_buffer};
    // gpio_init(ota_drive->BootPin, ota_drive->NumofDev, GPIO_MODE_OUTPUT);
    // gpio_init(ota_drive->RstPin, ota_drive->NumofDev, GPIO_MODE_OUTPUT);
    // gpio_set_level(*ota_drive->RstPin,1);
    // gpio_run_mcu(*ota_drive->BootPin,*ota_drive->RstPin);
    esp_err_t err = mount_file(ota_drive->base_path);
    if (err != ESP_OK)
        return err;
    err = read_image_file_version(ota_drive);
    if (err != ESP_OK)
        return err;
#if DEBUG
    ESP_LOGI(TAG, "Current version: %d.%d", ota_drive->current_version.major_version, ota_drive->current_version.minor_version);
#endif
    ota_drive->client = esp_http_client_init(&http_config);
    return ESP_OK;
}
esp_err_t drive_ota_get_new_image(drive_ota_handle_t *ota_drive, image_version_t *image_version)
{
    printf("Get image\r\n");
    int status_code, header_ret;
    // char *post_data = pvPortMalloc(100);
    esp_err_t err = ESP_OK;
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write iamge version");
        return err;
    }
    sprintf(ota_drive->download_link, DOWNLOAD_LINK_TEMPLATE, image_version->id);
    ESP_LOGI(TAG, "Link: %s", ota_drive->download_link);
    esp_http_client_set_url(ota_drive->client, ota_drive->download_link);
    do
    {
        char *post_data = NULL;
        /* Send POST request if body is set.
         * Note: Sending POST request is not supported if partial_http_download
         * is enabled
         */
        int post_len = esp_http_client_get_post_field(ota_drive->client, &post_data);
        printf("Post len: %d\r\n", post_len);
        err = esp_http_client_open(ota_drive->client, post_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            return ESP_FAIL;
        }
        if (post_len)
        {
            int write_len = 0;
            while (post_len > 0)
            {
                write_len = esp_http_client_write(ota_drive->client, post_data, post_len);
                if (write_len < 0)
                {
                    ESP_LOGE(TAG, "Write failed");
                    return ESP_FAIL;
                }
                post_len -= write_len;
                post_data += write_len;
            }
        }
        header_ret = esp_http_client_fetch_headers(ota_drive->client);
        if (header_ret < 0)
        {
            printf("Header ret: %d\r\n", header_ret);
            return header_ret;
        }
        status_code = esp_http_client_get_status_code(ota_drive->client);
        printf("fetch status code: %d\r\n", status_code);
        err = http_handle_response_code(ota_drive->client, status_code);
        if (err != ESP_OK)
        {
            printf("Handle response failed!\r\n");
            return err;
        }
    } while (process_again(status_code));
    // vPortFree(post_data)
    printf("Image length: %lld\r\n", esp_http_client_get_content_length(ota_drive->client));
    int data_read_size = esp_http_client_get_content_length(ota_drive->client);
    int data_read = 0, bytes_read = 0;
    int ota_buff_size = esp_http_client_get_content_length(ota_drive->client);
    ota_drive->buffer_size = (int32_t)ota_buff_size;
    ota_drive->bin_buffer = (char *)pvPortMalloc(ota_drive->buffer_size);
    if (ota_drive->bin_buffer == NULL)
    {
        printf("Can not alloc for ota_buff\r\n");
    }
    while (data_read_size > 0 && !esp_http_client_is_complete_data_received(ota_drive->client))
    {
        data_read = esp_http_client_read(ota_drive->client,
                                         ((char *)ota_drive->bin_buffer + bytes_read),
                                         data_read_size);
        printf("Data read: %d\r\n", data_read);
        if (data_read < 0)
        {
            if (data_read == -ESP_ERR_HTTP_EAGAIN)
            {
                ESP_LOGD(TAG, "ESP_ERR_HTTP_EAGAIN invoked: Call timed out before data was ready");
                continue;
            }
            ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
            break;
        }
        data_read_size -= data_read;
        bytes_read += data_read;
    }
    if (data_read_size > 0)
    {
        ESP_LOGE(TAG, "Complete headers were not received");
        return ESP_FAIL;
    }
    printf("File size: %ld\r\n", ota_drive->buffer_size);
    // printf("%s ",ota_buff);
    // sprintf(image->version.version,"%d.%d",ota_buff[0],ota_buff[1]);
    // sprintf(image->version.date,"%u/%u/20%u",ota_buff[2],ota_buff[3],ota_buff[4]);
    // sprintf(image->version.time,"%d:%d:%d",ota_buff[5],ota_buff[6],ota_buff[7]);
    // memcpy(&image->version, ota_buff, IMAGE_VERSION_SIZE);
    // ota_drive->buffer_size = (int32_t )ota_buff_size;
    // ota_drive->bin_buffer = (char *)pvPortMalloc(ota_drive->buffer_size + 10);
    // memcpy(ota_drive->bin_buffer, (char *)ota_buff,(size_t) ota_drive->buffer_size);
    // printf("%s \r\n",image->version);a
    err = drive_ota_update_image_version(ota_drive, image_version);
    return ESP_OK;
}
esp_err_t validate_image_version(drive_ota_handle_t *drive_ota, image_version_t *image_version)
{
    printf("version: %hhd.%hhd\r\n", image_version->major_version, image_version->minor_version);
    printf("Rej version: %hhd.%hhd\r\n", drive_ota->reject_version.major_version, drive_ota->reject_version.minor_version);
    if (image_version->major_version == drive_ota->reject_version.major_version && image_version->minor_version == drive_ota->reject_version.minor_version)
    {
        return ESP_FAIL;
    }
    if (image_version->major_version > drive_ota->current_version.major_version || image_version->minor_version > drive_ota->current_version.minor_version)
    {
        return ESP_OK;
    }
    return ESP_FAIL;
}
esp_err_t drive_ota_update_image_version(drive_ota_handle_t *drive_ota, image_version_t *image_version)
{
    // drive_ota->image = image;
    drive_ota->current_version = *image_version;
    char *version_buffer = (char *)pvPortMalloc(sizeof(*image_version) + 5);
    int buffer_size = sprintf(version_buffer, "%hhd.%hhd,%s", image_version->major_version, image_version->minor_version, image_version->id);
    printf("Buffer: %s\r\n", version_buffer);
    esp_err_t err = write_bin_file(drive_ota->file_version_path, version_buffer, buffer_size);
    vPortFree(version_buffer);
    return err;
}

esp_err_t drive_ota_flash_image(drive_ota_handle_t *drive_ota)
{
    // struct stat entry_stat;
    uint8_t binw[256];
    int lastbuf = 0, bini = 0;
    bini = drive_ota->buffer_size / 256;
    lastbuf = drive_ota->buffer_size % 256;
    gpio_init(drive_ota->BootPin, drive_ota->NumofDev, GPIO_MODE_OUTPUT);
    gpio_init(drive_ota->RstPin, drive_ota->NumofDev, GPIO_MODE_OUTPUT);
    // int64_t start_time_ms = esp_timer_get_time() / 1000UL;
    gpio_boot_mcu(*drive_ota->BootPin, *drive_ota->RstPin);
    initSTM32(drive_ota->uart_num);
    // #if DEBUG
    //     ESP_LOGI(TAG, "%ld", drive_ota->buffer_size);
    //     // ESP_LOGI(TAG, "%s", filename);
    //     // ESP_LOGI(TAG, "%s", drive_ota->file_bin_path);
    //     ESP_LOGI(TAG, "num buf : %d", bini);
    // #endif
    int i = 0;
    if (stm32Erase() == 1)
    {
        // httpd_resp_sendstr(req, "ERASE 1");
#if DEBUG
        ESP_LOGI(TAG, "ERASE DONE");
#endif
    }
    else
    {
#if DEBUG
        ESP_LOGI(TAG, "ERASE FAILED");
#endif
        return ESP_FAIL;
        // httpd_resp_send_500(req);
    }
    gpio_run_mcu(*drive_ota->BootPin, *drive_ota->RstPin);
    vTaskDelay(20 / portTICK_PERIOD_MS);
    gpio_boot_mcu(*drive_ota->BootPin, *drive_ota->RstPin);
    if (initSTM32(drive_ota->uart_num))
    {
        if (drive_ota->bin_buffer)
        {
            for (i = 0; i < bini; i++)
            {
                memcpy(binw, drive_ota->bin_buffer + 256 * i, 256);
                stm32SendCommand(STM32WR);
                if (stm32Address(STM32STADDR + (256 * i)))
                {
                    vTaskDelay(5/portTICK_PERIOD_MS);
#if DEBUG
                    if (stm32SendData(binw, 255))
                        ESP_LOGI(TAG, "data send success: %d", i);
                    else
                    {
                        ESP_LOGI(TAG, "data send failed : %d", i);
                        return ESP_FAIL;
                    }
#else
                    if (stm32SendData(binw, 255) == 0)
                    {
                        ESP_LOGI(TAG, "data send failed : %d", i);
                        return ESP_FAIL;
                    }
                    // httpd_resp_send_500(req);
#endif
                }
                else
                {
// #if DEBUG
                    ESP_LOGI(TAG, "send address failed");
// #endif
                    return ESP_FAIL;
                }
                vTaskDelay(10/portTICK_PERIOD_MS);
            }
            // fread(binw, 1, lastbuf, fd);
            memcpy(binw, drive_ota->bin_buffer + 256 * i, lastbuf);
            stm32SendCommand(STM32WR);
            if (stm32Address(STM32STADDR + (256 * bini)))
            {
                vTaskDelay(5/portTICK_PERIOD_MS);
#if DEBUG
                if (stm32SendData(binw, lastbuf))
                    ESP_LOGI(TAG, "last buf send success");
                else
                {
                    ESP_LOGI(TAG, "last buf send failed");
                    return ESP_FAIL;
                }
#else
                if (stm32SendData(binw, lastbuf) == 0)
                {
                    ESP_LOGI(TAG, "last buf send failed");
                    return ESP_FAIL;
                }
                // httpd_resp_send_500(req);
#endif
            }
            else
            {
#if DEBUG
                ESP_LOGI(TAG, "send address for last failed");
#endif
                return ESP_FAIL;
            }
            // fclose(fd);
        }
    }
    else
    {
#if DEBUG
        ESP_LOGI(TAG, "Init failed");
#endif
        return ESP_FAIL;
    }
    // ESP_LOGI(TAG, "Flash time: %lld", esp_timer_get_time() / 1000UL - start_time_ms);
    gpio_run_mcu(*drive_ota->BootPin, *drive_ota->RstPin);
    // drive_ota_get_new_image(drive_ota,)
    return ESP_OK;
}

esp_err_t drive_ota_get_new_image_version(drive_ota_handle_t *drive_ota, image_version_t *drive_version)
{
    printf("Check version\r\n");
    int status_code, header_ret;
    esp_err_t err;
    // esp_err_t err = esp_http_client_open(drive_ota->client, 0);
    // esp_http_client_set_header(drive_ota->client,"Content-Type","application/json");
    do
    {
        char *post_data = NULL;
        /* Send POST request if body is set.
         * Note: Sending POST request is not supported if partial_http_download
         * is enabled
         */
        int post_len = esp_http_client_get_post_field(drive_ota->client, &post_data);
        printf("Post len: %d\r\n", post_len);
        err = esp_http_client_open(drive_ota->client, post_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            return ESP_FAIL;
        }
        if (post_len)
        {
            int write_len = 0;
            while (post_len > 0)
            {
                write_len = esp_http_client_write(drive_ota->client, post_data, post_len);
                if (write_len < 0)
                {
                    ESP_LOGE(TAG, "Write failed");
                    return ESP_FAIL;
                }
                post_len -= write_len;
                post_data += write_len;
            }
        }
        header_ret = esp_http_client_fetch_headers(drive_ota->client);
        if (header_ret < 0)
        {
            printf("Header ret: %d\r\n", header_ret);
            return header_ret;
        }
        status_code = esp_http_client_get_status_code(drive_ota->client);
        printf("fetch status code: %d\r\n", status_code);
        err = http_handle_response_code(drive_ota->client, status_code);
        if (err != ESP_OK)
        {
            printf("Handle response failed!\r\n");
            return err;
        }
    } while (process_again(status_code));
    printf("%s\r\n", drive_ota->response_buffer);
    sscanf(drive_ota->response_buffer, "{\"name\":\"v%hhd.%hhd.bin\",\"id\":\"%[^\"]\"}", &drive_version->major_version, &drive_version->minor_version, drive_version->id);
    // printf("Version data: %s",cJSON_PrintUnformatted(JSON_Data));
    // char version_buffer[50] = "";
    // sprintf(version_buffer,"%s",cJSON_GetObjectItem(JSON_Data,"version")->valuestring);
    // sscanf(version_buffer,"%hhd.%hhd.bin",&drive_version->major_version,&drive_version->minor_version);
    printf("id: %s\r\n", drive_version->id);
    ESP_LOGI(TAG, "Version: %hhd.%hhd, %s", drive_version->major_version, drive_version->minor_version, drive_version->id);
    printf("New line\r\n");
    return ESP_OK;
}

void drive_ota_set_reject_version(drive_ota_handle_t *drive_ota, image_version_t *version)
{
    memcpy(&drive_ota->reject_version, version, sizeof(*version));
    char version_buffer[50] = "";
    int buffer_size = sprintf(version_buffer, "%hhd.%hhd,%s", drive_ota->reject_version.major_version, drive_ota->reject_version.minor_version, drive_ota->reject_version.id);
    printf("New old version: %s\r\n", version_buffer);
    ESP_ERROR_CHECK(write_bin_file(drive_ota->file_rej_version_path, version_buffer, buffer_size));
}
