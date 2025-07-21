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

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MABUTRACE";

static void* profiler_entries = NULL;
static volatile size_t entries_start_index = 0;
static volatile size_t entries_next_index = 0;
static volatile portMUX_TYPE profiler_index_mutex = portMUX_INITIALIZER_UNLOCKED;
static volatile uint16_t link_index = 0;
static volatile portMUX_TYPE link_index_mutex = portMUX_INITIALIZER_UNLOCKED;
static volatile TaskHandle_t task_handles[16];
static volatile uint8_t type_sizes[8];
static volatile bool tracing_enabled = false;
static volatile bool trace_interrupts_within_interrupted_tasks = false;
static SemaphoreHandle_t active_writers_semaphore; // Tracks in-flight writers

esp_err_t mabutrace_init() {
  if(profiler_entries)
    return ESP_ERR_INVALID_STATE;
#ifdef USE_PSRAM_IF_AVAILABLE
  profiler_entries = heap_caps_calloc(PROFILER_BUFFER_SIZE_IN_BYTES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  if(!profiler_entries)
    profiler_entries = calloc(PROFILER_BUFFER_SIZE_IN_BYTES, 1);
  if (!profiler_entries) {
    ESP_LOGE(TAG, "Failed to allocate %d bytes for trace buffer.", (int)PROFILER_BUFFER_SIZE_IN_BYTES);
    return ESP_ERR_NO_MEM;
  }
  else
    ESP_LOGI(TAG, "Allocated %d bytes for trace buffer.", (int)PROFILER_BUFFER_SIZE_IN_BYTES);
  memset(task_handles, 0, sizeof(task_handles));

  memset(type_sizes, 0, sizeof(type_sizes));
  type_sizes[EVENT_TYPE_NONE] = 0;
  type_sizes[EVENT_TYPE_DURATION] = sizeof(duration_entry_t);
  type_sizes[EVENT_TYPE_DURATION_COLORED] = sizeof(duration_colored_entry_t);
  type_sizes[EVENT_TYPE_INSTANT_COLORED] = sizeof(instant_colored_entry_t);
  type_sizes[EVENT_TYPE_COUNTER] = sizeof(counter_entry_t);
  type_sizes[EVENT_TYPE_LINK] = sizeof(link_entry_t);
  type_sizes[EVENT_TYPE_TASK_SWITCH_IN] = sizeof(task_switch_entry_t);
  type_sizes[EVENT_TYPE_TASK_SWITCH_OUT] = sizeof(task_switch_entry_t);

  #define MAX_CONCURRENT_WRITERS 255
  active_writers_semaphore = xSemaphoreCreateCounting(MAX_CONCURRENT_WRITERS, 0);
  if (active_writers_semaphore == NULL) {
      free(profiler_entries);
      profiler_entries = NULL;
      ESP_LOGE(TAG, "Failed to create active_writers_semaphore");
      return ESP_ERR_NO_MEM;
  }

  tracing_enabled = true;
  return ESP_OK;
}

esp_err_t mabutrace_deinit() {
  if(!profiler_entries)
    return ESP_ERR_INVALID_STATE;
  tracing_enabled = false;
  // Wait for writers to drain before deleting the semaphore
  while(uxSemaphoreGetCount(active_writers_semaphore) > 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
  }
  vSemaphoreDelete(active_writers_semaphore);
  active_writers_semaphore = NULL;
  free(profiler_entries);
  profiler_entries = NULL;
  return ESP_OK;
}

void set_trace_interrupts_within_interrupted_tasks(bool enabled) {
  trace_interrupts_within_interrupted_tasks = enabled;
}

static inline TaskHandle_t IRAM_ATTR get_current_task_handle() {
  if (!trace_interrupts_within_interrupted_tasks && xPortInIsrContext()) {
    return NULL;
  }
  else {
    return xTaskGetCurrentTaskHandle();
  }
}

static inline void semaphore_give(SemaphoreHandle_t semaphore, BaseType_t* must_yield_from_isr) {
  if (xPortInIsrContext()) {
    xSemaphoreGiveFromISR(semaphore, must_yield_from_isr);
  } else {
    xSemaphoreGive(semaphore);
  }
}

static inline void semaphore_take(SemaphoreHandle_t semaphore, BaseType_t* must_yield_from_isr) {
  if (xPortInIsrContext()) {
    xSemaphoreTakeFromISR(semaphore, must_yield_from_isr);
  } else {
    xSemaphoreTake(semaphore, portMAX_DELAY);
  }
}

static inline TaskHandle_t IRAM_ATTR get_task_handle_from_id(uint8_t id) {
  return task_handles[id];
}

static inline uint8_t IRAM_ATTR get_current_task_id() {
  TaskHandle_t handle = get_current_task_handle();
  if (!handle) {
    return 0;
  } else {
    for (uint8_t i = 1; i < 16; i++) {
      TaskHandle_t* handle_i = &task_handles[i];
      if (!*handle_i) {
        *handle_i = handle;
        return i;
      } else if (*handle_i == handle) {
        return i;
      }
    }
  }
  assert(false); // never get here.
  return 0;
}

static inline void IRAM_ATTR advance_pointers(uint8_t type_size, size_t* out_entry_idx) {
  taskENTER_CRITICAL(&profiler_index_mutex);
  {
    assert(entries_next_index <= PROFILER_BUFFER_SIZE_IN_BYTES);
    //critical section
    size_t start_idx = 0;
    size_t entry_idx = entries_next_index;
    //advance pointers
    if (PROFILER_BUFFER_SIZE_IN_BYTES - entry_idx < type_size) {
      // entry doesn't fit into end of buffer.
      // clear tail to indicate end.
      memset(profiler_entries + entries_next_index, 0, PROFILER_BUFFER_SIZE_IN_BYTES - entry_idx);
      // set entry_idx to start of buffer.
      entry_idx = 0;
      start_idx = 0;
      entries_next_index = type_size;
    }
    else {
      // fits in.
      start_idx = entries_start_index;
      entries_next_index = entry_idx + type_size;
    }
    // advance start_idx
    while (start_idx >= entry_idx && start_idx < entries_next_index) {
      entry_header_t* start_header = (entry_header_t*)(profiler_entries + start_idx);
      if (start_header->type == EVENT_TYPE_NONE) {
        start_idx = 0;
        break;
      }
      else {
        start_idx += type_sizes[start_header->type];
      }
    }
    if (start_idx == PROFILER_BUFFER_SIZE_IN_BYTES) {
      start_idx = 0;
    }
    entries_start_index = start_idx;
    *out_entry_idx = entry_idx;
  }
  taskEXIT_CRITICAL(&profiler_index_mutex);
}

static inline void IRAM_ATTR insert_link_event(uint16_t link, uint8_t link_type, uint64_t time_stamp, uint8_t cpu_id, uint8_t task_id) {
  if(!active_writers_semaphore)
    return;
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  size_t type_size = sizeof(link_entry_t);
  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);
  link_entry_t* entry = (link_entry_t*)(profiler_entries + entry_idx);
  entry->header.type = EVENT_TYPE_LINK;
  entry->header.cpu_id = cpu_id;
  entry->header.task_id = task_id;
  entry->time_stamp_begin_microseconds = (uint32_t)time_stamp;
  entry->link = link;
  entry->link_type = link_type;

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
}

const char* suspend_tracing_and_get_profiler_entries(size_t* out_start_idx, size_t* out_end_idx) {
  tracing_enabled = false;
  //Wait for all active writers to finish.
  while (uxSemaphoreGetCount(active_writers_semaphore) > 0) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  *out_start_idx = entries_start_index;
  *out_end_idx = entries_next_index;
  return (char*)profiler_entries;
}

void resume_tracing() {
  tracing_enabled = true;
}

const TaskHandle_t* profiler_get_task_handles() {
  assert(!tracing_enabled && "Must only call profiler_get_task_handles while tracing is suspended.");
  return task_handles;
}

profiler_duration_handle_t IRAM_ATTR trace_begin(const char* name, uint8_t color) {
  return trace_begin_linked(name, 0, NULL, color);
}

profiler_duration_handle_t IRAM_ATTR trace_begin_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color) {
  profiler_duration_handle_t result = {0};
  if(!active_writers_semaphore)
    return result;
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  result.time_stamp_begin_microseconds = esp_timer_get_time();
  result.name = name;
  result.link_in = link_in;
  result.color = color;
  if (link_out) {
    if (*link_out == 0) {
      taskENTER_CRITICAL(&link_index_mutex);
        //critical section
        result.link_out = ++link_index;
      taskEXIT_CRITICAL(&link_index_mutex);
      *link_out = result.link_out;
    } else {
      result.link_out = *link_out;
    }
  }
  else {
    result.link_out = 0;
  }

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
  return result;
}

void IRAM_ATTR trace_end(profiler_duration_handle_t* handle) {
  if(!active_writers_semaphore)
    return;
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = (uint8_t)xPortGetCoreID();
  uint64_t now = esp_timer_get_time();
  size_t type_size = 0;
  if (handle->color == 0) {
    type_size = sizeof(duration_entry_t);
  } else {
    type_size = sizeof(duration_colored_entry_t);
  }

  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);
  
  uint64_t duration = now - handle->time_stamp_begin_microseconds;
  if (handle->color == 0) {
    duration_entry_t* entry = (duration_entry_t*)(profiler_entries + entry_idx);
    entry->header.type = EVENT_TYPE_DURATION;
    entry->header.cpu_id = cpu_id;
    entry->header.task_id = task_id;
    entry->time_stamp_begin_microseconds = (uint32_t)handle->time_stamp_begin_microseconds;
    entry->time_duration_microseconds = duration;
    entry->name = handle->name;
  } else {
    duration_colored_entry_t* entry = (duration_colored_entry_t*)(profiler_entries + entry_idx);
    entry->header.type = EVENT_TYPE_DURATION_COLORED;
    entry->header.cpu_id = cpu_id;
    entry->header.task_id = task_id;
    entry->time_stamp_begin_microseconds = (uint32_t)handle->time_stamp_begin_microseconds;
    entry->time_duration_microseconds = duration;
    entry->name = handle->name;
    entry->color = handle->color;
  }
  if (handle->link_in) {
    insert_link_event(handle->link_in, LINK_TYPE_IN, handle->time_stamp_begin_microseconds-1, cpu_id, task_id);
  }
  if (handle->link_out) {
    insert_link_event(handle->link_out, LINK_TYPE_OUT, handle->time_stamp_begin_microseconds + duration - 1, cpu_id, task_id);
  }

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
}

void IRAM_ATTR trace_task_switch(uint8_t type) {
  if(!active_writers_semaphore)
    return;
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = (uint8_t)xPortGetCoreID();
  uint64_t now = esp_timer_get_time();
  size_t type_size = sizeof(task_switch_entry_t);

  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);

  task_switch_entry_t* entry = (task_switch_entry_t*)(profiler_entries + entry_idx);
  entry->header.type = type;
  entry->header.cpu_id = cpu_id;
  entry->header.task_id = task_id;
  entry->time_stamp = (uint32_t)now;

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
}

void IRAM_ATTR trace_flow_out(uint16_t* link_out, const char* name, uint8_t color) {
  if(!active_writers_semaphore)
    return;
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = (uint8_t)xPortGetCoreID();
  uint64_t now = esp_timer_get_time();

  if (link_out) {
    if (*link_out == 0) {
      taskENTER_CRITICAL(&link_index_mutex);
      //critical section
        *link_out = ++link_index;
      taskEXIT_CRITICAL(&link_index_mutex);
    }
  }
  if (link_out && *link_out) {
    insert_link_event(*link_out, LINK_TYPE_OUT, now, cpu_id, task_id);
  }

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
}

void IRAM_ATTR trace_flow_in(uint16_t link_in) {
  if(!active_writers_semaphore)
    return;
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = (uint8_t)xPortGetCoreID();
  uint64_t now = esp_timer_get_time();
  if (link_in) {
    insert_link_event(link_in, LINK_TYPE_IN, now, cpu_id, task_id);
  }

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
}

void IRAM_ATTR trace_instant(const char* name, uint8_t color) {
  trace_instant_linked(name, 0, NULL, color);
}

void IRAM_ATTR trace_instant_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color) {
  if(!active_writers_semaphore)
    return;
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = (uint8_t)xPortGetCoreID();
  uint64_t now = esp_timer_get_time();
  size_t type_size = sizeof(instant_colored_entry_t);

  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);

  instant_colored_entry_t* entry = (instant_colored_entry_t*)(profiler_entries + entry_idx);
  entry->header.type = EVENT_TYPE_INSTANT_COLORED;
  entry->header.cpu_id = cpu_id;
  entry->header.task_id = task_id;
  entry->time_stamp_begin_microseconds = (uint32_t)now;
  entry->name = name;
  entry->color = color;

  if (link_out) {
    if (*link_out == 0) {
      taskENTER_CRITICAL(&link_index_mutex);
      //critical section
        *link_out = ++link_index;
      taskEXIT_CRITICAL(&link_index_mutex);
    }
  }

  if (link_in) {
    insert_link_event(link_in, LINK_TYPE_IN, now, cpu_id, task_id);
  }
  if (link_out && *link_out) {
    insert_link_event(*link_out, LINK_TYPE_OUT, now, cpu_id, task_id);
  }

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
}

void IRAM_ATTR trace_counter(const char* name, int32_t value, uint8_t color) {
  BaseType_t must_yield_from_isr = pdFALSE;
  semaphore_give(active_writers_semaphore, &must_yield_from_isr);
  if(!tracing_enabled) {
    goto cleanup;
  }

  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = (uint8_t)xPortGetCoreID();
  uint64_t now = esp_timer_get_time();
  size_t type_size = sizeof(counter_entry_t);

  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);

  counter_entry_t* entry = (counter_entry_t*)(profiler_entries + entry_idx);
  entry->header.type = EVENT_TYPE_COUNTER;
  entry->header.cpu_id = cpu_id;
  entry->header.task_id = task_id;
  entry->time_stamp_begin_microseconds = (uint32_t)now;
  entry->name = name;
  entry->value = value;

  cleanup:
  semaphore_take(active_writers_semaphore, &must_yield_from_isr);
  if(must_yield_from_isr)
    portYIELD_FROM_ISR();
}