idf_component_register(SRCS "src/OE_https_ota.c" "src/OE_ota_utils.c" 
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_client bootloader_support esp_app_format
                    PRIV_REQUIRES log app_update )
