#include "../mabutrace.h"

#include <string.h>

#include "esp_timer.h"

#define configUSE_TRACE_FACILITY 1
#define traceTASK_DELAY_UNTIL() TRACE_SCOPE("vTaskDelayUntil")
#define traceTASK_DELAY() TRACE_SCOPE("vTaskDelay")

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void* profiler_entries = NULL;
static volatile size_t entries_start_index = 0;
static volatile size_t entries_next_index = 0;
static volatile portMUX_TYPE profiler_index_mutex = portMUX_INITIALIZER_UNLOCKED;
static volatile uint16_t link_index = 0;
static volatile portMUX_TYPE link_index_mutex = portMUX_INITIALIZER_UNLOCKED;
static volatile TaskHandle_t task_handles[16];
static volatile uint8_t type_sizes[8];
static volatile size_t buffer_size_in_bytes;

void profiler_init() {
  profiler_init_with_size(PROFILER_BUFFER_SIZE_IN_BYTES);
}

void profiler_init_with_size(size_t ring_buffer_size_in_bytes) {
  buffer_size_in_bytes = ring_buffer_size_in_bytes;
  if(profiler_entries)
    return;
#ifdef USE_PSRAM_IF_AVAILABLE
  profiler_entries = heap_caps_calloc(ring_buffer_size_in_bytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
  if(!profiler_entries)
    profiler_entries = calloc(ring_buffer_size_in_bytes, 1);
  if (!profiler_entries)
    ets_printf("Failed to allocate trace buffer.\n");
  else
    ets_printf("Allocated %d bytes for trace buffer.\n", (int)ring_buffer_size_in_bytes);
  memset(task_handles, 0, sizeof(task_handles));

  memset(type_sizes, 0, sizeof(type_sizes));
  type_sizes[EVENT_TYPE_NONE] = 0;
  type_sizes[EVENT_TYPE_DURATION] = sizeof(duration_entry_t);
  type_sizes[EVENT_TYPE_DURATION_COLORED] = sizeof(duration_colored_entry_t);
  type_sizes[EVENT_TYPE_INSTANT_COLORED] = sizeof(instant_colored_entry_t);
  type_sizes[EVENT_TYPE_COUNTER] = sizeof(counter_entry_t);
  type_sizes[EVENT_TYPE_LINK] = sizeof(link_entry_t);
}

size_t get_smallest_type_size() {
  assert(("Profiler has not been initialized", proffiler_entries));
  size_t min_size = 1000;
  for(int i=0; i<sizeof(type_sizes); i++) {
    size_t size = type_sizes[i];
    if(size != 0 && size < min_size)
      min_size = size;
  }
  return min_size;
}

size_t get_smallest_type_size() {
  assert(("Profiler has not been initialized", proffiler_entries));
  size_t min_size = 1000;
  for(int i=0; i<sizeof(type_sizes); i++) {
    size_t size = type_sizes[i];
    if(size != 0 && size < min_size)
      min_size = size;
  }
  return min_size;
}

void profiler_deinit() {
  if(!profiler_entries)
    return;
  taskENTER_CRITICAL(&profiler_index_mutex);
  free(profiler_entries);
  profiler_entries = NULL;
  taskEXIT_CRITICAL(&profiler_index_mutex);
}

size_t get_buffer_size() {
  return PROFILER_BUFFER_SIZE_IN_BYTES;
}

inline TaskHandle_t IRAM_ATTR get_current_task_handle() {
  if (xPortInIsrContext()) {
    return NULL;
  }
  else {
    return xTaskGetCurrentTaskHandle();
  }
}

inline TaskHandle_t IRAM_ATTR get_task_handle_from_id(uint8_t id) {
  return task_handles[id];
}

inline uint8_t IRAM_ATTR get_current_task_id() {
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

inline void IRAM_ATTR advance_pointers(uint8_t type_size, size_t* out_entry_idx) {
  if(!profiler_entries)
    return;
  taskENTER_CRITICAL(&profiler_index_mutex);
  {
    assert(entries_next_index <= buffer_size_in_bytes);
    //critical section
    size_t start_idx = 0;
    size_t entry_idx = entries_next_index;
    //advance pointers
    if (buffer_size_in_bytes - entry_idx < type_size) {
      // entry doesn't fit into end of buffer.
      // clear tail to indicate end.
      memset(profiler_entries + entries_next_index, 0, buffer_size_in_bytes - entry_idx);
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
    if (start_idx == buffer_size_in_bytes) {
      start_idx = 0;
    }
    entries_start_index = start_idx;
    *out_entry_idx = entry_idx;
  }
  taskEXIT_CRITICAL(&profiler_index_mutex);
}

inline void IRAM_ATTR insert_link_event(uint16_t link, uint8_t link_type, uint64_t time_stamp, uint8_t cpu_id, uint8_t task_id) {
  if(!profiler_entries)
    return;
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
}

void profiler_get_entries(void* output_buffer, size_t* out_start_idx, size_t* out_end_idx) {
  if(!profiler_entries)
    return;
  taskENTER_CRITICAL(&profiler_index_mutex);
    memcpy(output_buffer, profiler_entries, buffer_size_in_bytes);
    *out_start_idx = entries_start_index;
    *out_end_idx = entries_next_index;
  taskEXIT_CRITICAL(&profiler_index_mutex);
}

size_t get_num_task_handles() {
  return 16;
}

void profiler_get_task_handles(void* output_taskhandle_16) {
  memcpy(output_taskhandle_16, task_handles, sizeof(TaskHandle_t) * 16);
}

profiler_duration_handle_t IRAM_ATTR trace_begin(const char* name, uint8_t color) {
  return trace_begin_linked(name, 0, NULL, color);
}

profiler_duration_handle_t IRAM_ATTR trace_begin_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color) {
  profiler_duration_handle_t result;
  if(!profiler_entries)
    return result;
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
  return result;
}

void IRAM_ATTR trace_end(profiler_duration_handle_t* handle) {
  if(!profiler_entries)
    return;
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
}

void IRAM_ATTR trace_instant(const char* name, uint8_t color) {
  trace_instant_linked(name, 0, NULL, color);
}

void IRAM_ATTR trace_instant_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color) {
  if(!profiler_entries)
    return;
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
}

void IRAM_ATTR trace_counter(const char* name, int32_t value, uint8_t color) {
  if(!profiler_entries)
    return;
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
}