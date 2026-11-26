idf_component_register(SRCS "music_service.c" "image_pixels.c" INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_settings net audio_in lyrics recognizer esp_http_client mbedtls esp_timer esp_driver_jpeg espressif__cjson)

target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/jpeg_color_test.jpg" BINARY)
