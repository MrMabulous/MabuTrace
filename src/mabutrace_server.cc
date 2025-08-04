/*
 * Copyright (C) 2020 Matthias Bühlmann
 *
 * This file is part of MabuTrace.
 *
 * MabuTrace is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MabuTrace is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MabuTrace.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "mabutrace.h"

#include "download_website.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "MABUTRACE";

void process_chunk(void* ctx, const char* chunk, size_t size) {
    httpd_req_t* req = (httpd_req_t*)ctx;
    assert(chunk[size] == '\0' && "Expected to be null terminated");

    if (httpd_resp_send_chunk(req, chunk, size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send chunk");
    }
}

esp_err_t request_handler_chunked(httpd_req_t *req) {
    ESP_LOGI(TAG, "download request received.");
    // Get the json string of trace
    // Set the correct content type for JSON
    httpd_resp_set_type(req, "application/json");
    // Send the response
    if(get_json_trace_chunked((void*)req, process_chunk) != ESP_OK) {
        httpd_resp_send_500(req); // Convenience function for 500
        return ESP_OK;
    }
    // Send the final, zero-length chunk to signify the end of the response
    if (httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send final chunk");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mabutrace_start_server(int port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority++;
    int default_port = config.server_port;
    config.server_port = port;
    config.ctrl_port += (port - default_port);
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