/* General Includes */
#include "ota-server.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Http server */
// #include "esp_eth.h"
#include "esp_netif.h"
// #include "esp_tls_crypto.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
// #include "protocol_examples_common.h"
#include <sys/param.h>
#include "esp_wifi.h"

/* Http client - OTA */
#include "esp_http_client.h"
#include "OE_https_ota.h"
#include "wifi_functions.h"
#include "math.h"

static const char *TAG = "OTA SERVER";

/* -------- CLIENT ------------ */

// receive buffer
char rcv_buffer[200];
char url_str[100];

// esp_http_client event handler
esp_err_t
_http_event_handler(esp_http_client_event_t *evt) {

	switch (evt->event_id) {
		case HTTP_EVENT_ERROR:
			break;
		case HTTP_EVENT_ON_CONNECTED:
			break;
		case HTTP_EVENT_HEADER_SENT:
			break;
		case HTTP_EVENT_ON_HEADER:
			break;
		case HTTP_EVENT_ON_DATA:
			if (!esp_http_client_is_chunked_response(evt->client)) {
				strncpy(rcv_buffer, (char *)evt->data, evt->data_len);
			}
			break;
		case HTTP_EVENT_ON_FINISH:
			break;
		case HTTP_EVENT_DISCONNECTED:
			break;
		case HTTP_EVENT_REDIRECT:
			break;
	}
	return ESP_OK;
}

// bool
// check_version_string(uint8_t *ver_str) {
// 	/* int i; */
// 	unsigned dot_counter = 0;
// 	for (unsigned i = 0; i < strlen((const char *)ver_str); i++) {
// 		if (ver_str[i] == '.')
// 			dot_counter++;
// 	}
// 	printf("Num of dots: %d\n", dot_counter);
// 	if (dot_counter == 2) {
// 		return true;
// 	} else {
// 		return false;
// 	}
// }

// bool
// str2num(uint8_t *str, uint8_t len, uint8_t *num) {
// 	*num = 0;

// 	for (unsigned i = 0; i < len; i++) {
// 		*num = *num + (str[i] - '0') * (int)(pow(10, len - (i + 1)));
// 	}

// 	return true;
// }

// bool
// get_version_from_string(uint8_t *version, uint8_t *build, uint8_t *major,
// 						uint8_t *minor) {
// 	uint8_t buil[3];
// 	uint8_t maj[3];
// 	uint8_t min[3];
// 	if (check_version_string(version)) {
// 		/* uint8_t num_of_dec = 0; */
// 		uint8_t i = 0;
// 		uint8_t j = 0;
// 		while (version[i] != '.') {
// 			buil[j] = version[i];
// 			i++;
// 			j++;
// 		}
// 		str2num(buil, j, build);
// 		printf("build: %d\n", *build);
// 		i++;
// 		j = 0;
// 		while (version[i] != '.') {
// 			maj[j] = version[i];
// 			i++;
// 			j++;
// 		}
// 		str2num(maj, j, major);
// 		printf("major: %d\n", *major);
// 		i++;
// 		j = 0;
// 		while (version[i] != 0) {
// 			min[j] = version[i];
// 			i++;
// 			j++;
// 		}
// 		str2num(min, j, minor);
// 		printf("minor: %d\n", *minor);
// 		return true;
// 	}
// 	return false;
// }

// /* function to check a newer version */
// bool
// check_version(uint8_t *new_version) {
// 	uint8_t build_new;
// 	uint8_t major_new;
// 	uint8_t minor_new;

// 	/* convert new version to int */
// 	printf("New version:\n");
// 	get_version_from_string(new_version, &build_new, &major_new, &minor_new);

// 	/* compare versions */
// 	if (build_new < BUILD) {
// 		return false;
// 	} else if (build_new > BUILD) {
// 		return true;
// 	}
// 	if (major_new < MAJOR) {
// 		return false;
// 	} else if (major_new > MAJOR) {
// 		return true;
// 	}
// 	if (minor_new <= MINOR) {
// 		return false;
// 	} else {
// 		return true;
// 	}
// }

// Check update task
// downloads every 30sec the json file with the latest firmware
void
check_update_task(void *ptr) {

	unsigned attempts = 1;
	while (attempts <=3) {
		ESP_LOGI(TAG,
			 "Current Firmware Version %s\n\n", CONFIG_APP_PROJECT_VER);
		ESP_LOGI(TAG, "Looking for a new firmware...\n");
		ESP_LOGI(TAG, "URL: %s\n", (char *)url_str);
		ESP_LOGI(TAG, "Attemp: %d\n", attempts);

		esp_http_client_config_t ota_client_config
			= { .url = (char *)url_str,
			    .skip_cert_common_name_check = true};
		esp_err_t ret = esp_https_ota(&ota_client_config);
		if (ret == ESP_OK) {
			ESP_LOGI(TAG, "OTA SUCESS...\n");
			esp_restart();
			break;
		} else {
			ESP_LOGE(TAG, "OTA failed...\n");
			if (attempts == 3) {
				ESP_LOGE(TAG, "MAX NUMBER OF ATTEMPTS... ABORTING OTA");
				break;
			}
		}
		printf("\n");
		vTaskDelay(10000 / portTICK_PERIOD_MS);

		attempts++;
	}
	vTaskDelete(NULL);
}

/* --------------------------- SERVER ----------------------------------- */
/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

/* An HTTP POST handler
 * The client shall send his host ip server */
static esp_err_t
ota_post_handler(httpd_req_t *req) {
	char buf[100];
	int ret, remaining = req->content_len;
	memset(buf, 0x00, sizeof(buf));
	while (remaining > 0) {
		/* Read the data for the request */
		if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf))))
			<= 0) {
			if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
				/* Retry receiving if timeout occurred */
				continue;
			}
			return ESP_FAIL;
		}
		/* Log data received */
		ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
		ESP_LOGI(TAG, "%.*s", ret, buf);
		ESP_LOGI(TAG, "====================================");
		memcpy(url_str, buf, strlen(buf)+1);
		/* Enable OTA Task to download the binary file */
		xTaskCreate(&check_update_task,
					"check_update_task",
					8192,
					NULL,
					5,
					NULL);
		/* Send back the same data */
		httpd_resp_send_chunk(req, buf, ret);
		remaining -= ret;
	}
	// End response
	httpd_resp_send_chunk(req, NULL, 0);
	return ESP_OK;
}

static const httpd_uri_t ota = { .uri = "/ota",
								  .method = HTTP_POST,
								  .handler = ota_post_handler,
								  .user_ctx = NULL };

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
// esp_err_t
// http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
// 	char msg[50];
// 	sprintf(msg, "Get request for path %s is no available!",req->uri);
// 	httpd_resp_send_err(req,
// 							HTTPD_404_NOT_FOUND,
// 							msg);
// 	return ESP_FAIL;
// }

static httpd_handle_t
start_webserver(void) {
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.lru_purge_enable = true;

	// Start the httpd server
	ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
	if (httpd_start(&server, &config) == ESP_OK) {
		// Set URI handlers
		ESP_LOGI(TAG, "Registering URI handlers");
		httpd_register_uri_handler(server, &ota);
		return server;
	}

	ESP_LOGI(TAG, "Error starting server!");
	return NULL;
}

static void
stop_webserver(httpd_handle_t server) {
	// Stop the httpd server
	httpd_stop(server);
}

static void
disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
				   void *event_data) {
	httpd_handle_t *server = (httpd_handle_t *)arg;
	if (*server) {
		ESP_LOGI(TAG, "Stopping webserver");
		stop_webserver(*server);
		*server = NULL;
	}
}

static void
connect_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
				void *event_data) {
	httpd_handle_t *server = (httpd_handle_t *)arg;
	if (*server == NULL) {
		ESP_LOGI(TAG, "Starting webserver");
		*server = start_webserver();
	}
}

void
init_ota_server() {

	printf("HTTPS OTA, current firmware %s\n\n", CONFIG_APP_PROJECT_VER);

	static httpd_handle_t server = NULL;
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());

	// Connect to the wifi network
	wifi_initialize();
	printf("Connected to wifi network\n");

	/* Register event handlers to stop the server when Wi-Fi or Ethernet is
	 * disconnected, and re-start it upon connection.
	 */
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
											   IP_EVENT_STA_GOT_IP,
											   &connect_handler,
											   &server));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
											   WIFI_EVENT_STA_DISCONNECTED,
											   &disconnect_handler,
											   &server));

	/* Start the server for the first time */
	server = start_webserver();
	while (1) {
		/* Delay */
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
