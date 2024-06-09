idf_component_register(SRCS "main.c" "src/drive_ota.c"  "src/spiffs_mount.c" "src/uart.c" "src/stm32_ota.c" "src/ssd1306.c" "src/wifi_connect.c" "src/gpio.c"
                    INCLUDE_DIRS "."
                                 "include"
                    EMBED_TXTFILES ${project_dir}/certs/google.cer)

spiffs_create_partition_image(storage ../spiffs_dir FLASH_IN_PROJECT)