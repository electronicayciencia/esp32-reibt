idf_component_register(SRCS "bt_app_av.c"
                            "bt_app_core.c"
                            "main.c"
                            "volume.c"
                            "reconnect.c"
                    PRIV_REQUIRES esp_driver_i2s bt nvs_flash esp_ringbuf
                    INCLUDE_DIRS ".")
