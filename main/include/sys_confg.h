#ifndef _SYS_CONFIG_H_
#define _SYS_CONFIG_H_

#define DEBUG                       0
#define DRIVE_URL                   "https://script.google.com/macros/s/AKfycbzD-GMxh2b-GkOLigWCm2XdLslcmMjOQVesCwQTl3lBcgMAoYatcJ8SU-QmkV0R-B3xdA/exec"
#define DOWNLOAD_LINK_TEMPLATE      "https://drive.google.com/u/0/uc?id=%s&export=download"
#define MAX_AUTH_RETRIES            10
#define MAX_HTTP_OUTPUT_BUFFER      1024
#define UART                        UART_NUM_2

// #define WIFI_SSID                   "Far From Home"
// #define WIFI_PASS                   "05041993"

#define WIFI_SSID                   "CEEC_Tenda"
#define WIFI_PASS                   "1denmuoi1"

#define BASE_PATH                   "/spiffs_dir"
#define VERSION_PATH                "/spiffs_dir/version.txt"
#define REJ_VERSION_PATH            "/spiffs_dir/rej_version.txt"

#define MAX_FLASH_RETRIES           10

#define SLEEP_TIME_S                5 * 1000000UL 

#define ACCEPT_BIT                  BIT1
#define REJECT_BIT                  BIT2

#define ACCEPT_BTN                  15
#define REJECT_BTN                  23
#define BOOT_PIN                    18
#define RST_PIN                     5
#define SDA_PIN                     21
#define SCL_PIN                     22
#endif