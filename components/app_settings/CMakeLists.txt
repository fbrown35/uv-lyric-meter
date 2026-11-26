idf_component_register(SRCS "app_settings.c" INCLUDE_DIRS "include"
                       REQUIRES esp_common PRIV_REQUIRES nvs_flash esp_driver_usb_serial_jtag espressif__cjson)
