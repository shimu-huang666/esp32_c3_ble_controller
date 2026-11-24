idf_component_register(SRCS "main.c" "ble_controller.c" "servo_control.c" "ota_update.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bt nvs_flash esp_driver_ledc app_update esp_app_format)
