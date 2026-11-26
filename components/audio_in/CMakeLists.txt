idf_component_register(SRCS "audio_in.c" "audio_ring.c"
                       INCLUDE_DIRS "include"
                       REQUIRES level_meter esp_driver_i2c
                       PRIV_REQUIRES esp_driver_i2s esp_driver_gpio espressif__esp_codec_dev freertos)
