# Vendored from the GUITION JC8012P4A1 demo. gsl_point_id.c is Silead code under GPL-2.0+;
# see components/THIRD_PARTY.md.
idf_component_register(SRCS "esp_lcd_touch_gsl3680.c" "gsl_point_id.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_lcd"
                       PRIV_REQUIRES "esp_driver_gpio" "esp_driver_i2c")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable)
