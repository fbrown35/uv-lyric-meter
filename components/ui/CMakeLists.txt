idf_component_register(SRCS "analog_meter.c" "digital_meter.c" "vinyl.c" "ui_font.c" "music_ui.c" INCLUDE_DIRS "include"
                       REQUIRES lvgl__lvgl PRIV_REQUIRES heap audio_in level_meter esp_timer app_settings music_service board)

target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/assets/fonts/Pretendard-Regular.ttf" BINARY)
