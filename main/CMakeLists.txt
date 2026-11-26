idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES app_settings music_service net ui board audio_in level_meter esp_hw_support esp_system esp_timer spi_flash
                                     esp_lvgl_port)

# LVGL's LV_ATTRIBUTE_FAST_MEM (IRAM) is applied to both declaration and definition with different
# section names; GCC 15 in IDF 6 flags that as -Wattributes. Harmless, so don't fail the build on it.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-error=attributes -Wno-attributes)
