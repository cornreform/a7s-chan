idf_component_register(SRCS "main.cpp"
                            "udp_client.cpp"
                            "expressions.cpp"
                            "face_renderer.cpp"
                            "servo_control.cpp"
                            "ir_control.cpp"
                            "led_control.cpp"
                            "audio_pipeline.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_netif lwip rmt driver
)
