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

#include <WiFi.h>
#include <mabutrace.h>

const char *ssid = "*****";
const char *password = "*****";

void ARDUINO_ISR_ATTR onTimer();
void ARDUINO_ISR_ATTR randomFill(char* buf, size_t length);
bool findLongestPalindrome(char* buf, size_t length);
void workerTask(void *pvParameters);

// Define timer
hw_timer_t *timer = NULL;

// Define Queues
QueueHandle_t Queue1, Queue2;
typedef struct {
  char line[400];
  uint16_t link;
} message_t;

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.print("\n\nConnecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");

  //Setup timer frequency of 1Mhz
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  // Set alarm to call onTimer function 100 times per second (value in microseconds).
  // Repeat the alarm (third parameter) with unlimited count = 0 (fourth parameter).
  timerAlarm(timer, 10000, true, 0);

  //Setup queues to pass data from one task to another
  Queue1 = xQueueCreate(2, sizeof(message_t));
  Queue2 = xQueueCreate(2, sizeof(message_t));

  //Setup Task
  //xTaskCreatePinnedToCore(
  xTaskCreate(
    workerTask, "Worker Task 1",  // A name just for humans
    2048,  // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    NULL,  // Task parameter which can modify the task behavior. This must be passed as pointer to void.
    2,  // Priority
    NULL  // Task handle is not used here - simply pass NULL
    //0  // Run on core 0
  );
  xTaskCreate(
    workerTask, "Worker Task 2",  // A name just for humans
    2048,  // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    NULL,  // Task parameter which can modify the task behavior. This must be passed as pointer to void.
    2,  // Priority
    NULL  // Task handle is not used here - simply pass NULL
    //0  // Run on core 0
  );

  //Initialize MabuTrace and start server on port 81
  mabutrace_init();
  mabutrace_start_server(81);

  Serial.print("MabuTrace server started. Go to ");
  Serial.print(WiFi.localIP());
  Serial.println(":81/ to capture a trace.");
}

void ARDUINO_ISR_ATTR onTimer() {
  //It's fine to issue traces from interrupts.
  TRC();
  message_t message;
  randomFill(message.line, sizeof(message.line));

  //To trace application flow accross thread boundaries TRACE_FLOW_OUT and TRACE_FLOW_IN can be used.
  //First an outbound flow trace is created by passing a pointer to a uint16_t that's set to 0 as link_out argument:
  uint16_t link_idx = 0;
  TRACE_FLOW_OUT(&link_idx);
  //link_idx now contains a value that needs to be used as link_in argument when tracing the corresponding inbound flow trace.
  message.link = link_idx;

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if(xQueueSendFromISR(Queue1, &message, &xHigherPriorityTaskWoken) == errQUEUE_FULL) {
    //Instead of scopes, which represent durations, instant events can be traced using TRACE_INSTANT.
    //All TRACE macros accept an optional color argument to set the color to be used for the trace explicitly.
    TRACE_INSTANT("Queue1 Full", COLOR_DARK_RED);
  }
  if(xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void ARDUINO_ISR_ATTR randomFill(char* buf, size_t length) {
  //TRC() here is equivalent to TRACE_SCOPE("randomFill");
  //TRC is a shortcut to issue a TRACE_SCOPE using the name of the function it is called from.
  TRC();
  for(int i=0; i<length-1; i++) {
    //Assign random character from A-Z
    buf[i] = random(65, 91);
  }
  buf[length-1] = '\0';
}

//Takes messages from Queue1, sorts characters alphabetically and then adds them to Queue2
void workerTask(void *pvParameters) {
  for (;;) {
    TRACE_SCOPE("Worker Task loop");
    message_t message;
    {
      //Trace xQueueReceive separately so we see in the trace when the task is blocked by an empty queue.
      TRACE_SCOPE("xQueueReceive Queue1", COLOR_YELLOW);
      xQueueReceive(Queue1, &message, portMAX_DELAY);
    }
    //Trace the inbound half of the flow trace using the link value as link_in argument.
    TRACE_FLOW_IN(message.link);

    if(findLongestPalindrome(message.line, sizeof(message.line))) {
      //Create another outbound flow trace
      uint16_t link_idx = 0;
      TRACE_FLOW_OUT(&link_idx);
      //link_idx now contains a value that needs to be used as link_in argument when tracing the corresponding inbound linked trace.
      message.link = link_idx;
      {
        //Trace xQueueSend separately so we see in the trace when the task is blocked by a full queue.
        TRACE_SCOPE("xQueueSend Queue2", COLOR_YELLOW);
        xQueueSend(Queue2, &message, portMAX_DELAY);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool findLongestPalindrome(char* buf, size_t len) {
  //TRC() here is equivalent to TRACE_SCOPE("findLongestPalindrome");
  //TRC is a shortcut to issue a TRACE_SCOPE using the name of the function it is called from.
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

void loop() {
  //TRC() here is equivalent to TRACE_SCOPE("loop");
  //TRC is a shortcut to issue a TRACE_SCOPE using the name of the function it is called from.
  TRC();
  message_t message;
  {
    //Trace xQueueReceive separately so we see in the trace when the task is blocked by an empty queue.
    TRACE_SCOPE("xQueueReceive Queue2", COLOR_YELLOW);
    xQueueReceive(Queue2, &message, portMAX_DELAY);
  }
  //print the received message
  //Trace the inbound half of the linked trace using the link value as link_in argument.
  TRACE_FLOW_IN(message.link);
  {
    TRACE_SCOPE("Serial.println");
    Serial.println(message.line);
  }
}