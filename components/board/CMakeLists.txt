idf_component_register(SRCS "board.c" "display_scan.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c lvgl__lvgl
                       PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_lcd esp_hw_support esp_mm
                                     espressif__esp_lcd_jd9365 esp_lcd_touch_gsl3680 esp_lvgl_port)
