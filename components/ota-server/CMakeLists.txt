idf_component_register(
	SRCS "ota-server.c" "wifi_functions.c" "cJSON.c"
	INCLUDE_DIRS "include"
	REQUIRES driver nvs_flash esp_http_server esp_http_client OE_https_ota esp_wifi
	)
