idf_component_register(SRCS "app_main.c"
                       REQUIRES crt_core crt_demo crt_diag crt_fb crt_hal crt_timing
        PRIV_REQUIRES esp_driver_uart
                       INCLUDE_DIRS ".")
