#include "mabutrace_server.h"

#include "mabutrace_export.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "mabutrace_server";

esp_err_t request_handler(httpd_req_t *req) {
    // Get the json string of trace
    size_t json_buffer_size = get_json_size();
    ESP_LOGI(TAG, "json_buffer_size: %d", json_buffer_size);
    char* json_buffer = malloc(json_buffer_size);
    get_json_trace(json_buffer, json_buffer_size);
    ESP_LOGI(TAG, "json: %s", json_buffer);
    // Set the correct content type for JSON
    httpd_resp_set_type(req, "application/json");
    // Send the response
    httpd_resp_send(req, json_buffer, HTTPD_RESP_USE_STRLEN);
    free(json_buffer);
    return ESP_OK;
}

esp_err_t start_mabutrace_server(int port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    httpd_handle_t server_handle;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server_handle, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server!");
        return ESP_FAIL;
    }

    httpd_uri_t root_uri = {
        .uri       = "/",      // Handle requests to the root URL
        .method    = HTTP_GET,
        .handler   = request_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server_handle, &root_uri);

    ESP_LOGI(TAG, "Server started.");
    return ESP_OK;
}