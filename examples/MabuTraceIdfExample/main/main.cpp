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

#include <stdio.h>
#include <string.h>

#include "driver/gptimer.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

#include "mabutrace.h"

const char *ssid = "*****";
const char *password = "*****";

// ESP-IDF Logging Tag
static const char *TAG = "MabuTraceExample";

// Forward declarations
void IRAM_ATTR randomFill(char *buf, size_t length);
bool findLongestPalindrome(char *buf, size_t length);
void workerTask(void *pvParameters);

// Define Temperature sensor handle
#if SOC_TEMP_SENSOR_SUPPORTED
temperature_sensor_handle_t temp_handle = NULL;
#endif

// Define timer handle for ESP-IDF
gptimer_handle_t gptimer = NULL;

// Define Queues
QueueHandle_t Queue1, Queue2;
typedef struct {
  char line[400];
  uint16_t link;
} message_t;

// Semaphore to signal Wi-Fi connection
static SemaphoreHandle_t wifi_connected_sem;

// Wi-Fi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Retrying connection to the AP");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    xSemaphoreGive(wifi_connected_sem);
  }
}

void wifi_init_sta(void) {
  wifi_connected_sem = xSemaphoreCreateBinary();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {};
  strcpy((char *)wifi_config.sta.ssid, ssid);
  strcpy((char *)wifi_config.sta.password, password);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");
  printf("Connecting to %s ", ssid);

  // Wait for connection
  xSemaphoreTake(wifi_connected_sem, portMAX_DELAY);
  printf("\nWiFi connected.\n");
}

// Timer interrupt callback, executed every 10ms
static bool IRAM_ATTR onTimer(gptimer_handle_t timer,
                              const gptimer_alarm_event_data_t *edata,
                              void *user_ctx) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  // It's fine to issue traces from interrupts.
  TRC();
  message_t message;
  randomFill(message.line, sizeof(message.line));

  // To trace application flow accross thread boundaries TRACE_FLOW_OUT and
  // TRACE_FLOW_IN can be used. First an outbound flow trace is created by
  // passing a pointer to a uint16_t that's set to 0 as link_out argument:
  uint16_t link_idx = 0;
  TRACE_FLOW_OUT(&link_idx);
  // link_idx now contains a value that needs to be used as link_in argument
  // when tracing the corresponding inbound flow trace.
  message.link = link_idx;

  if (xQueueSendFromISR(Queue1, &message, &xHigherPriorityTaskWoken) ==
      errQUEUE_FULL) {
    // Instead of scopes, which represent durations, instant events can be
    // traced using TRACE_INSTANT. All TRACE macros accept an optional color
    // argument to set the color to be used for the trace explicitly.
    TRACE_INSTANT("Queue1 Full", COLOR_DARK_RED);
  }

  return (xHigherPriorityTaskWoken == pdTRUE);
}

// Fill buffer with random string consisting of characters A-Z
void IRAM_ATTR randomFill(char *buf, size_t length) {
  // TRC() here is equivalent to TRACE_SCOPE("randomFill");
  // TRC is a shortcut to issue a TRACE_SCOPE using the name of the function it
  // is called from.
  TRC();
  {
    TRACE_SCOPE("esp_fill_random");
    esp_fill_random(buf, length - 1);
  }
  {
    TRACE_SCOPE("map range");
    // Upper case ASCII letters are from values A=65 - Z=90
    for (int i = 0; i < length - 1; i++) {
      uint8_t c = buf[i];
      buf[i] = c % 26 + 65;
    }
    buf[length - 1] = '\0';
  }
}

// Naive implementation to find longest palindrome in given buffer and write
// result into buffer
bool findLongestPalindrome(char *buf, size_t len) {
  // TRC() here is equivalent to TRACE_SCOPE("findLongestPalindrome");
  // TRC is a shortcut to issue a TRACE_SCOPE using the name of the function it
  // is called from.
  TRC();
  if (buf == NULL || len < 2) {
    return false;
  }
  for (int l = len - 1; l >= 2; l--) {
    for (char *s = buf; s <= buf + len - l; s++) {
      char *p1 = s;
      char *p2 = s + l - 1;
      while (p1 < p2 && *p1 == *p2) {
        p1++;
        p2--;
      }
      if (p1 >= p2) {
        memmove(buf, s, l);
        buf[l] = '\0';
        return true;
      }
    }
  }
  return false;
}

// Worker task that takes messages from Queue1, searches for longest palindorme
// and then posts result to Queue2
void workerTask(void *pvParameters) {
  for (;;) {
    TRACE_SCOPE("Worker Task loop");
    message_t message;
    {
      // Trace xQueueReceive separately so we see in the trace when the task is
      // blocked by an empty queue.
      TRACE_SCOPE("xQueueReceive Queue1", COLOR_YELLOW);
      xQueueReceive(Queue1, &message, portMAX_DELAY);
    }
    // Trace the inbound half of the flow trace using the link value as link_in
    // argument.
    TRACE_FLOW_IN(message.link);

    if (findLongestPalindrome(message.line, sizeof(message.line))) {
      // Create another outbound flow trace
      uint16_t link_idx = 0;
      TRACE_FLOW_OUT(&link_idx);
      // link_idx now contains a value that needs to be used as link_in argument
      // when tracing the corresponding inbound linked trace.
      message.link = link_idx;
      {
        // Trace xQueueSend separately so we see in the trace when the task is
        // blocked by a full queue.
        TRACE_SCOPE("xQueueSend Queue2", COLOR_YELLOW);
        xQueueSend(Queue2, &message, portMAX_DELAY);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

extern "C" void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize WiFi
  wifi_init_sta();

#if SOC_TEMP_SENSOR_SUPPORTED
  // Setup temperature sensor
  temperature_sensor_config_t temp_sensor_config =
      TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
  ESP_ERROR_CHECK(
      temperature_sensor_install(&temp_sensor_config, &temp_handle));
  ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
#endif

  // Setup timer
  gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000,  // 1MHz, 1 tick = 1us
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

  gptimer_event_callbacks_t cbs = {
      .on_alarm = onTimer,
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

  gptimer_alarm_config_t alarm_config = {
      .alarm_count = 10000,  // period = 10ms
      .reload_count = 0,
      .flags =
          {
              .auto_reload_on_alarm = true,
          },
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
  ESP_ERROR_CHECK(gptimer_enable(gptimer));
  ESP_ERROR_CHECK(gptimer_start(gptimer));

  // Setup queues to pass data from one task to another
  Queue1 = xQueueCreate(2, sizeof(message_t));
  Queue2 = xQueueCreate(2, sizeof(message_t));

  // Setup as many worker tasks as there are cpu cores
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  char worker_task_name[16];
  for (int i = 0; i < chip_info.cores; i++) {
    snprintf(worker_task_name, sizeof(worker_task_name), "Worker Task %d", i);
    xTaskCreate(workerTask, worker_task_name, 2048, NULL, 2, NULL);
  }

  // Initialize MabuTrace and start server on port 81
  ESP_ERROR_CHECK(mabutrace_init());
  ESP_ERROR_CHECK(mabutrace_start_server(81));

  // Get IP Address
  esp_netif_ip_info_t ip_info;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_get_ip_info(netif, &ip_info);

  printf("MabuTrace server started. Go to " IPSTR ":81/ to capture a trace.\n",
         IP2STR(&ip_info.ip));

  for (;;) {
    TRC();  // Equivalent to TRACE_SCOPE("loop_task");
#if SOC_TEMP_SENSOR_SUPPORTED
    {
      TRACE_SCOPE("Read temperature");
      float temp_out;
      ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &temp_out));
      int temp_rounded = (int)(temp_out + 0.5f);
      TRACE_COUNTER("Temperature", temp_rounded);
    }
#endif
    message_t message;
    {
      // Trace xQueueReceive separately so we see in the trace when the task is
      // blocked by an empty queue.
      TRACE_SCOPE("xQueueReceive Queue2", COLOR_YELLOW);
      xQueueReceive(Queue2, &message, portMAX_DELAY);
    }
    // print the received message
    // Trace the inbound half of the linked trace using the link value as
    // link_in argument.
    TRACE_FLOW_IN(message.link);
    {
      TRACE_SCOPE("printf");
      // print palindromes longer than 7 chars
      if (strnlen(message.line, sizeof(message.line)) > 7) {
        printf("Palindrome generated: %s\n", message.line);
      }
    }
  }
}