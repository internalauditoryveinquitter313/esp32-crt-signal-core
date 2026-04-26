idf_component_register(SRCS "crt_hal.c" "crt_hal_clock.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_i2s esp_hw_support)
