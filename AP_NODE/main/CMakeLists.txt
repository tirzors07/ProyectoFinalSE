idf_component_register(SRCS "ws_echo_server.c"
                    INCLUDE_DIRS ".")
spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)