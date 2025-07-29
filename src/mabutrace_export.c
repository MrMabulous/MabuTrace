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

#include <assert.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "MABUTRACE";

// An upper bound estimate for the number of chars required for the json output of each entry
#define MAX_CHARS_PER_ENTRY 512

static const char* json_header = "{\n"
                                 "  \"traceEvents\": [\n";
static const char* json_footer = "    {\"name\": \"process_name\", \"ph\": \"M\", \"pid\": 1, \"args\": {\"name\": \"Tasks & Interrupts\"}},\n"
                                 "    {\"name\": \"process_name\", \"ph\": \"M\", \"pid\": 2, \"args\": {\"name\": \"CPU Task Scheduling\"}},\n"
                                 "    {\"name\": \"process_sort_index\", \"ph\": \"M\", \"pid\": 1, \"args\": {\"sort_index\": 0}},\n"
                                 "    {\"name\": \"process_sort_index\", \"ph\": \"M\", \"pid\": 2, \"args\": {\"sort_index\": 1}}\n"
                                 "  ],\n"
                                 "  \"displayTimeUnit\": \"ms\",\n"
                                 "  \"otherData\": {\n"
                                 "    \"version\": \"MabuTrace Profiler v1.0\"\n"
                                 "  }\n"
                                 "}";

static const char* colorNameLookup[] = {
  "",                                        // COLOR_UNDEFINED
  ",\"cname\":\"good\"",                     // COLOR_GREEN
  ",\"cname\":\"vsync_highlight_color\"",    // COLOR_LIGHT_GREEN
  ",\"cname\":\"bad\"",                      // COLOR_DARK_ORANGE
  ",\"cname\":\"terrible\"",                 // COLOR_DARK_RED
  ",\"cname\":\"yellow\"",                   // COLOR_YELLOW
  ",\"cname\":\"olive\"",                    // COLOR_OLIVE
  ",\"cname\":\"black\"",                    // COLOR_BLACK
  ",\"cname\":\"white\"",                    // COLOR_WHITE
  ",\"cname\":\"generic_work\"",             // COLOR_GRAY
  ",\"cname\":\"grey\""                      // COLOR_LIGHT_GRAY
};

esp_err_t get_json_trace_chunked(void* ctx, void (*process_chunk)(void*, const char*, size_t)) {
  esp_err_t res = ESP_OK;
  char buf[MAX_CHARS_PER_ENTRY];
  size_t start_idx;
  size_t end_idx;
  const char* profiler_entries = suspend_tracing_and_get_profiler_entries(&start_idx, &end_idx);
  const TaskHandle_t* task_handles = profiler_get_task_handles();

  size_t lineLength = snprintf(buf, sizeof(buf), "%s", json_header);
  assert(lineLength >= 0 && lineLength < sizeof(buf) && "Failed to correctly write header.");
  process_chunk(ctx, buf, lineLength);

  size_t idx = start_idx;
  size_t loopCount = 0;
  int entry_counter = 0;
  do {
    entry_header_t* entry_header = (entry_header_t*)(profiler_entries + idx);
    if(entry_header->type == EVENT_TYPE_NONE) {
      idx = 0;
      loopCount++;
      continue;
    }

    const char* threadName = pcTaskGetName(task_handles[entry_header->task_id]);
    if(entry_header->task_id == 0) {
      threadName = (entry_header->cpu_id == 0) ? "ISR On CPU 0" : "ISR On CPU 1";
    }
    size_t entry_size;
    switch (entry_header->type) {
      case EVENT_TYPE_DURATION: {
        duration_entry_t* entry = (duration_entry_t*)entry_header;
        entry_size = sizeof(duration_entry_t);
        lineLength = snprintf(buf, sizeof(buf), "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":1,\"tid\":\"%s\",\"ts\":%llu,\"dur\":%llu,\"args\":{\"cpu\":%d}},\n",
                              entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (unsigned long long int)entry->time_duration_microseconds, (int)entry_header->cpu_id);
        break;
      }
      case EVENT_TYPE_DURATION_COLORED: {
        duration_colored_entry_t* entry = (duration_colored_entry_t*)entry_header;
        entry_size = sizeof(duration_colored_entry_t);
        lineLength = snprintf(buf, sizeof(buf), "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":1,\"tid\":\"%s\",\"ts\":%llu,\"dur\":%llu,\"args\":{\"cpu\":%d}%s},\n",
                              entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (unsigned long long int)entry->time_duration_microseconds, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_INSTANT_COLORED: {
        instant_colored_entry_t* entry = (instant_colored_entry_t*)entry_header;
        entry_size = sizeof(instant_colored_entry_t);
        lineLength = snprintf(buf, sizeof(buf), "    {\"name\":\"%s\",\"ph\":\"i\",\"pid\":1,\"tid\":\"%s\",\"ts\":%llu,\"s\":\"p\",\"args\":{\"cpu\":%d}%s},\n",
                              entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_COUNTER: {
        counter_entry_t* entry = (counter_entry_t*)entry_header;
        entry_size = sizeof(counter_entry_t);
        lineLength = snprintf(buf, sizeof(buf), "    {\"name\":\"%s\",\"ph\":\"C\",\"pid\":1,\"tid\":\"%s\",\"ts\":%llu,\"args\":{\"value\":%d}},\n",
                              entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (int)entry->value);
        break;
      }
      case EVENT_TYPE_LINK: {
        link_entry_t* entry = (link_entry_t*)entry_header;
        entry_size = sizeof(link_entry_t);
        char phase = (entry->link_type == LINK_TYPE_IN) ? 'f' : 's';
        lineLength = snprintf(buf, sizeof(buf), "    {\"name\":\"flow\",\"cat\":\"flow\",\"id\":%u,\"ph\":\"%c\",\"pid\":1,\"tid\":\"%s\",\"ts\":%llu},\n",
                              (unsigned int)entry->link, phase, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds);
        break;
      }
      case EVENT_TYPE_TASK_SWITCH_IN:
      case EVENT_TYPE_TASK_SWITCH_OUT: {
        task_switch_entry_t* entry = (task_switch_entry_t*)entry_header;
        entry_size = sizeof(task_switch_entry_t);
        char phase = (entry_header->type == EVENT_TYPE_TASK_SWITCH_IN) ? 'B' : 'E';
        char* cpu_name = (entry_header->cpu_id == 0) ? "CPU 0" : "CPU 1";
        // Using the CPU name as tid since this doesn't track a particular task but task execution on a particular CPU core
        lineLength = snprintf(buf, sizeof(buf), "    {\"name\":\"%s\",\"cat\":\"task\",\"ph\":\"%c\",\"pid\":2,\"tid\":\"%s\",\"ts\":%llu},\n",
                             threadName, phase, cpu_name, (unsigned long long int)entry->time_stamp);
        break;
      }
      case EVENT_TYPE_NONE:
      default: {
        int type = entry_header->type;
        ESP_LOGE(TAG, "invalid event type: %d\n", type);
        res = ESP_ERR_INVALID_STATE;
        goto cleanup;
        break;
      }
    }
    assert(lineLength >= 0 && lineLength < sizeof(buf) && "Failed to correctly write line.");
    process_chunk(ctx, buf, lineLength);

    // advance idx
    idx += entry_size;
    if(idx >= PROFILER_BUFFER_SIZE_IN_BYTES) {
      loopCount++;
      idx = 0;
    }
    entry_counter++;
    if(entry_counter % 100==0) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  } while (idx != end_idx && loopCount <= 1);

  lineLength = snprintf(buf, sizeof(buf),"%s", json_footer);
  assert(lineLength >= 0 && lineLength < sizeof(buf) && "Failed to correctly write footer.");
  process_chunk(ctx, buf, lineLength);

  cleanup:
  resume_tracing();
  return res;
}