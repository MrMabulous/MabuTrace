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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// An upper bound estimate for the number of chars required for the json output of each entry
#define MAX_CHARS_PER_ENTRY 256

static const char* json_header = "{\n"
                                 "  \"traceEvents\": [\n";
static const char* json_footer = "    {}\n"
                                 "  ],\n"
                                 "  \"displayTimeUnit\": \"ms\",\n"
                                 "  \"otherData\": {\n"
                                 "    \"version\": \"MabuTrace Profiler v1.0\"\n"
                                 "  }\n"
                                 "}";

static const size_t header_and_footer_bytes = 128; // rounded up

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

size_t get_json_size() {
  // compute conservative buffer size.
  size_t min_type_size = get_smallest_type_size();
  size_t max_number_elements = get_buffer_size() / min_type_size;
  size_t json_buffer_size = header_and_footer_bytes + max_number_elements * MAX_CHARS_PER_ENTRY;
  return json_buffer_size;
}

void get_json_trace(char* json_buffer, size_t json_buffer_size) {
  assert(json_buffer_size >= get_json_size() && "Buffer too small.");
  char* profiler_entries;
  const size_t profiler_buffer_size = get_buffer_size();
  profiler_entries = (char*)malloc(profiler_buffer_size);

  size_t start_idx;
  size_t end_idx;
  profiler_get_entries(profiler_entries, &start_idx, &end_idx);
  size_t num_task_handles = get_num_task_handles();
  TaskHandle_t* task_handles = (TaskHandle_t*)malloc(num_task_handles * sizeof(TaskHandle_t));
  profiler_get_task_handles(task_handles);

  size_t lineLength = sprintf(json_buffer, "%s", json_header);
  
  char* chunk = json_buffer + lineLength;
  size_t ofst = 0;
  size_t idx = start_idx;
  size_t loopCount = 0;
  do {
    entry_header_t* entry_header = (entry_header_t*)(profiler_entries + idx);
    if(entry_header->type == EVENT_TYPE_NONE) {
      idx = 0;
      loopCount++;
      continue;
    }

    const char* threadName = pcTaskGetName(task_handles[entry_header->task_id]);
    if(entry_header->task_id == 0) {
      threadName = (char*)"INTERRUPT";
    }
    size_t entry_size;
    switch (entry_header->type) {
      case EVENT_TYPE_DURATION: {
        duration_entry_t* entry = (duration_entry_t*)entry_header;
        entry_size = sizeof(duration_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"dur\":%llu,\"args\":{\"cpu\":%d}},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (unsigned long long int)entry->time_duration_microseconds, (int)entry_header->cpu_id);
        break;
      }
      case EVENT_TYPE_DURATION_COLORED: {
        duration_colored_entry_t* entry = (duration_colored_entry_t*)entry_header;
        entry_size = sizeof(duration_colored_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"dur\":%llu,\"args\":{\"cpu\":%d}%s},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (unsigned long long int)entry->time_duration_microseconds, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_INSTANT_COLORED: {
        instant_colored_entry_t* entry = (instant_colored_entry_t*)entry_header;
        entry_size = sizeof(instant_colored_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"i\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"s\":\"p\",\"args\":{\"cpu\":%d}%s},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_COUNTER: {
        counter_entry_t* entry = (counter_entry_t*)entry_header;
        entry_size = sizeof(counter_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"C\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"args\":{\"value\":%d}},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (int)entry->value);
        break;
      }
      case EVENT_TYPE_LINK: {
        link_entry_t* entry = (link_entry_t*)entry_header;
        entry_size = sizeof(link_entry_t);
        char phase = (entry->link_type == LINK_TYPE_IN) ? 'f' : 's';
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"flow\",\"cat\":\"flow\",\"id\":%u,\"ph\":\"%c\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu},\n",
                            (unsigned int)entry->link, phase, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds); 
        break;
      }
      case EVENT_TYPE_NONE:
      default:
        int type = entry_header->type;
        printf("invalid event type: %d\n", type);
        assert(false);
        break;
    }
    ofst += lineLength;
    assert(ofst <= json_buffer_size && "Buffer overflow.");

    // advance idx
    idx += entry_size;
    if(idx >= profiler_buffer_size) {
      loopCount++;
      idx = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  } while (idx != end_idx && loopCount <= 1);

  lineLength = sprintf(chunk + ofst, "%s", json_footer);

  free(profiler_entries);
  free(task_handles);
}

void get_json_trace_chunked(void* ctx, void (*process_chunk)(void*, const char*, size_t)) {
  char buf[MAX_CHARS_PER_ENTRY];
  char* profiler_entries;
  const size_t profiler_buffer_size = get_buffer_size();
  profiler_entries = (char*)malloc(profiler_buffer_size);

  size_t start_idx;
  size_t end_idx;
  profiler_get_entries(profiler_entries, &start_idx, &end_idx);
  size_t num_task_handles = get_num_task_handles();
  TaskHandle_t* task_handles = (TaskHandle_t*)malloc(num_task_handles * sizeof(TaskHandle_t));
  profiler_get_task_handles(task_handles);

  size_t lineLength = sprintf(buf, "%s", json_header);
  process_chunk(ctx, buf, lineLength);

  size_t idx = start_idx;
  size_t loopCount = 0;
  do {
    entry_header_t* entry_header = (entry_header_t*)(profiler_entries + idx);
    if(entry_header->type == EVENT_TYPE_NONE) {
      idx = 0;
      loopCount++;
      continue;
    }

    const char* threadName = pcTaskGetName(task_handles[entry_header->task_id]);
    if(entry_header->task_id == 0) {
      threadName = (char*)"INTERRUPT";
    }
    size_t entry_size;
    switch (entry_header->type) {
      case EVENT_TYPE_DURATION: {
        duration_entry_t* entry = (duration_entry_t*)entry_header;
        entry_size = sizeof(duration_entry_t);
        lineLength = sprintf(buf, "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"dur\":%llu,\"args\":{\"cpu\":%d}},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (unsigned long long int)entry->time_duration_microseconds, (int)entry_header->cpu_id);
        break;
      }
      case EVENT_TYPE_DURATION_COLORED: {
        duration_colored_entry_t* entry = (duration_colored_entry_t*)entry_header;
        entry_size = sizeof(duration_colored_entry_t);
        lineLength = sprintf(buf, "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"dur\":%llu,\"args\":{\"cpu\":%d}%s},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (unsigned long long int)entry->time_duration_microseconds, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_INSTANT_COLORED: {
        instant_colored_entry_t* entry = (instant_colored_entry_t*)entry_header;
        entry_size = sizeof(instant_colored_entry_t);
        lineLength = sprintf(buf, "    {\"name\":\"%s\",\"ph\":\"i\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"s\":\"p\",\"args\":{\"cpu\":%d}%s},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_COUNTER: {
        counter_entry_t* entry = (counter_entry_t*)entry_header;
        entry_size = sizeof(counter_entry_t);
        lineLength = sprintf(buf, "    {\"name\":\"%s\",\"ph\":\"C\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu,\"args\":{\"value\":%d}},\n",
                             entry->name, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds, (int)entry->value);
        break;
      }
      case EVENT_TYPE_LINK: {
        link_entry_t* entry = (link_entry_t*)entry_header;
        entry_size = sizeof(link_entry_t);
        char phase = (entry->link_type == LINK_TYPE_IN) ? 'f' : 's';
        lineLength = sprintf(buf, "    {\"name\":\"flow\",\"cat\":\"flow\",\"id\":%u,\"ph\":\"%c\",\"pid\":0,\"tid\":\"%s\",\"ts\":%llu},\n",
                            (unsigned int)entry->link, phase, threadName, (unsigned long long int)entry->time_stamp_begin_microseconds); 
        break;
      }
      case EVENT_TYPE_NONE:
      default:
        int type = entry_header->type;
        printf("invalid event type: %d\n", type);
        assert(false);
        break;
    }
    assert(lineLength <= MAX_CHARS_PER_ENTRY && "Buffer overflow.");
    process_chunk(ctx, buf, lineLength);

    // advance idx
    idx += entry_size;
    if(idx >= profiler_buffer_size) {
      loopCount++;
      idx = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  } while (idx != end_idx && loopCount <= 1);

  lineLength = sprintf(buf, "%s", json_footer);
  process_chunk(ctx, buf, lineLength);

  free(profiler_entries);
  free(task_handles);
}