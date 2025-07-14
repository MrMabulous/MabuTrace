#include "mabutrace_server.h"

#include "mabutrace_export.h"
#include "download_website.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "mabutrace_server";

esp_err_t request_handler(httpd_req_t *req) {
    // Get the json string of trace
    size_t json_buffer_size = get_json_size();
    ESP_LOGI(TAG, "json_buffer_size: %d", json_buffer_size);
    char* json_buffer = (char*)malloc(json_buffer_size);
    get_json_trace(json_buffer, json_buffer_size);
    ESP_LOGI(TAG, "json: %s", json_buffer);
    // Set the correct content type for JSON
    httpd_resp_set_type(req, "application/json");
    // Send the response
    httpd_resp_send(req, json_buffer, HTTPD_RESP_USE_STRLEN);
    free(json_buffer);
    return ESP_OK;
}


void process_chunk(void* ctx, const char* chunk, size_t size) {
    httpd_req_t* req = (httpd_req_t*)ctx;
    if (httpd_resp_send_chunk(req, chunk, size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send chunk");
    }
}

esp_err_t request_handler_chunked(httpd_req_t *req) {
    // Get the json string of trace
    // Set the correct content type for JSON
    httpd_resp_set_type(req, "application/json");
    // Send the response
    //get_json_trace_chunked((void*)req, process_chunk);
    get_json_trace_chunked((void*)req, process_chunk);

    // Send the final, zero-length chunk to signify the end of the response
    if (httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send final chunk");
        return ESP_FAIL;
    }

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
        .handler   = [](httpd_req_t *req){
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, download_html, HTTPD_RESP_USE_STRLEN);            
        },
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server_handle, &root_uri);

    httpd_uri_t trace_uri = {
        .uri       = "/trace.json",      // Handle requests to the root URL
        .method    = HTTP_GET,
        .handler   = request_handler_chunked,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server_handle, &trace_uri);

    ESP_LOGI(TAG, "Server started.");
    return ESP_OK;
}