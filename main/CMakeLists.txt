idf_component_register(SRCS "main.c" "ble_controller.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bt nvs_flash)
