idf_component_register(SRCS "esp_zigbee_gateway.c"
                    PRIV_REQUIRES ieee802154 nvs_flash esp_driver_uart esp_timer
                    INCLUDE_DIRS ".")
