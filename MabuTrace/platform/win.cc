#include "../mabutrace.h"

#include <assert.h>
#include <windows.h>
#include <processthreadsapi.h>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#ifndef PROFILER_BUFFER_SIZE_IN_BYTES
  #error specify PROFILER_BUFFER_SIZE_IN_BYTES
#endif

typedef DWORD TaskHandle_t;

static char* profiler_entries = nullptr;
static size_t entries_start_index = 0;
static size_t entries_next_index = 0;
static std::mutex profiler_index_mutex;
static uint16_t link_index = 0;
static std::mutex link_index_mutex;
//static TaskHandle_t task_handles[16];
static std::shared_mutex task_handle_mutex;
static uint8_t task_handle_counter = 0;
static std::unordered_map<TaskHandle_t, uint8_t> task_handles;
static std::unordered_map<uint8_t, TaskHandle_t> reverse_task_handles;
static uint8_t type_sizes[8];
static size_t buffer_size_in_bytes;
static LARGE_INTEGER start_time;
static LARGE_INTEGER performance_counter_frequency;

inline void ENTER_CRITICAL(std::mutex* mutex) {
    mutex->lock();
}
inline void EXIT_CRITICAL(std::mutex* mutex) {
    mutex->unlock();
}
inline void ENTER_CRITICAL(std::shared_mutex* mutex) {
    mutex->lock();
}
inline void EXIT_CRITICAL(std::shared_mutex* mutex) {
    mutex->unlock();
}

inline void ENTER_CRITICAL_WRITE(std::shared_mutex* mutex) {
    mutex->lock();
}
inline void EXIT_CRITICAL_WRITE(std::shared_mutex* mutex) {
    mutex->unlock();
}
inline void ENTER_CRITICAL_READ(std::shared_mutex* mutex) {
    mutex->lock_shared();
}
inline void EXIT_CRITICAL_READ(std::shared_mutex* mutex) {
    mutex->unlock_shared();
}

inline void profiler_init() {
  profiler_init_with_size(PROFILER_BUFFER_SIZE_IN_BYTES);
}

void profiler_init_with_size(size_t ring_buffer_size_in_bytes) {
  QueryPerformanceCounter(&start_time);
  QueryPerformanceFrequency(&performance_counter_frequency); 
  buffer_size_in_bytes = ring_buffer_size_in_bytes;
  if(profiler_entries)
    return;
    profiler_entries = (char*)calloc(buffer_size_in_bytes, 1);
  if (!profiler_entries)
    printf("Failed to allocate trace buffer.\n");
  else
    printf("Allocated %lld bytes for trace buffer.\n", (long long)buffer_size_in_bytes);

  memset(type_sizes, 0, sizeof(type_sizes));
  type_sizes[EVENT_TYPE_NONE] = 0;
  type_sizes[EVENT_TYPE_DURATION] = sizeof(duration_entry_t);
  type_sizes[EVENT_TYPE_DURATION_COLORED] = sizeof(duration_colored_entry_t);
  type_sizes[EVENT_TYPE_INSTANT_COLORED] = sizeof(instant_colored_entry_t);
  type_sizes[EVENT_TYPE_COUNTER] = sizeof(counter_entry_t);
  type_sizes[EVENT_TYPE_LINK] = sizeof(link_entry_t);
}

size_t get_smallest_type_size() {
  assert(("Profiler has not been initialized", profiler_entries));
  size_t min_size = 1000;
  for(int i=0; i<sizeof(type_sizes); i++) {
    size_t size = type_sizes[i];
    if(size != 0 && size < min_size)
      min_size = size;
  }
  return min_size;
}

size_t get_timestamp_frequency() {
  return static_cast<size_t>(performance_counter_frequency.QuadPart);
}

void profiler_deinit() {
  if(!profiler_entries)
    return;
  ENTER_CRITICAL(&profiler_index_mutex);
  free(profiler_entries);
  profiler_entries = nullptr;
  EXIT_CRITICAL(&profiler_index_mutex);
}

size_t get_buffer_size() {
  return buffer_size_in_bytes;
}

inline TaskHandle_t get_current_task_handle() {
  return GetCurrentThreadId();
}

inline uint8_t get_cpu_id() {
  DWORD full_id = GetCurrentProcessorNumber();
  assert(("CPU id is too high.", full_id <= MAX_CPU_ID));
  return (uint8_t)full_id;
}

inline TaskHandle_t get_task_handle_from_id(uint8_t id) {
  ENTER_CRITICAL_READ(&task_handle_mutex);
  TaskHandle_t result = reverse_task_handles[id];
  EXIT_CRITICAL_READ(&task_handle_mutex);
  return result;
}

inline uint64_t get_now() {
    LARGE_INTEGER right_now, elapsed;
    QueryPerformanceCounter(&right_now);
    elapsed.QuadPart = right_now.QuadPart - start_time.QuadPart;
    return static_cast<uint64_t>(elapsed.QuadPart);
}

inline uint8_t get_current_task_id() {
  TaskHandle_t handle = get_current_task_handle();
  if (!handle) {
    return 0;
  }
  uint8_t res;
  ENTER_CRITICAL_READ(&task_handle_mutex);
  std::unordered_map<TaskHandle_t, uint8_t>::const_iterator it = task_handles.find(handle);
  if(it != task_handles.end()) {
    res = it->second;
    EXIT_CRITICAL_READ(&task_handle_mutex);
  } else {
    EXIT_CRITICAL_READ(&task_handle_mutex);
    ENTER_CRITICAL_WRITE(&task_handle_mutex);
      assert(("Too many different threads.", task_handle_counter < MAX_THREAD_ID));
      task_handle_counter++;
      task_handles[handle] = task_handle_counter;
      reverse_task_handles[task_handle_counter] = handle;
      res = task_handle_counter;
    EXIT_CRITICAL_WRITE(&task_handle_mutex);
  }
  return res;
}

inline void advance_pointers(uint8_t type_size, size_t* out_entry_idx) {
  if(!profiler_entries)
    return;
  ENTER_CRITICAL(&profiler_index_mutex);
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
  EXIT_CRITICAL(&profiler_index_mutex);
}

inline void insert_link_event(uint16_t link, uint8_t link_type, uint64_t time_stamp, uint8_t cpu_id, uint8_t task_id) {
  if(!profiler_entries)
    return;
  size_t type_size = sizeof(link_entry_t);
  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);
  link_entry_t* entry = (link_entry_t*)(profiler_entries + entry_idx);
  entry->header.type = EVENT_TYPE_LINK;
  entry->header.cpu_id = cpu_id;
  entry->header.task_id = task_id;
  entry->time_stamp_begin = time_stamp;
  entry->link = link;
  entry->link_type = link_type;
}

void profiler_get_entries(void* output_buffer, size_t* out_start_idx, size_t* out_end_idx) {
  if(!profiler_entries)
    return;
  ENTER_CRITICAL(&profiler_index_mutex);
    memcpy(output_buffer, profiler_entries, buffer_size_in_bytes);
    *out_start_idx = entries_start_index;
    *out_end_idx = entries_next_index;
  EXIT_CRITICAL(&profiler_index_mutex);
}

size_t get_num_task_handles() {
  return task_handles.size();
}

void profiler_get_task_handles(TaskHandle_t* output_taskhandle) {
  // memcpy(output_taskhandle_16, task_handles, sizeof(TaskHandle_t) * 16);
  output_taskhandle[0] = 0;
  for(int i=1; i < task_handles.size(); i++) {
    output_taskhandle[i] = reverse_task_handles[i];
  }
}

profiler_duration_handle_t trace_begin(const char* name, uint8_t color) {
  return trace_begin_linked(name, 0, nullptr, color);
}

profiler_duration_handle_t trace_begin_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color) {
  profiler_duration_handle_t result;
  if(!profiler_entries)
    return result;
  result.time_stamp_begin = get_now();
  result.name = name;
  result.link_in = link_in;
  result.color = color;
  if (link_out) {
    if (*link_out == 0) {
      ENTER_CRITICAL(&link_index_mutex);
        //critical section
        result.link_out = ++link_index;
      EXIT_CRITICAL(&link_index_mutex);
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

void trace_end(profiler_duration_handle_t* handle) {
  if(!profiler_entries)
    return;
  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = get_cpu_id();
  uint64_t now = get_now();
  size_t type_size = 0;
  if (handle->color == 0) {
    type_size = sizeof(duration_entry_t);
  } else {
    type_size = sizeof(duration_colored_entry_t);
  }

  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);
  
  uint64_t duration = now - handle->time_stamp_begin;
  if (handle->color == 0) {
    duration_entry_t* entry = (duration_entry_t*)(profiler_entries + entry_idx);
    entry->header.type = EVENT_TYPE_DURATION;
    entry->header.cpu_id = cpu_id;
    entry->header.task_id = task_id;
    entry->time_stamp_begin = handle->time_stamp_begin;
    entry->time_duration = duration;
    entry->name = handle->name;
  } else {
    duration_colored_entry_t* entry = (duration_colored_entry_t*)(profiler_entries + entry_idx);
    entry->header.type = EVENT_TYPE_DURATION_COLORED;
    entry->header.cpu_id = cpu_id;
    entry->header.task_id = task_id;
    entry->time_stamp_begin = handle->time_stamp_begin;
    entry->time_duration = duration;
    entry->name = handle->name;
    entry->color = handle->color;
  }
  if (handle->link_in) {
    insert_link_event(handle->link_in, LINK_TYPE_IN, handle->time_stamp_begin-1, cpu_id, task_id);
  }
  if (handle->link_out) {
    insert_link_event(handle->link_out, LINK_TYPE_OUT, handle->time_stamp_begin + duration - 1, cpu_id, task_id);
  }
}

void trace_instant(const char* name, uint8_t color) {
  trace_instant_linked(name, 0, nullptr, color);
}

void trace_instant_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color) {
  if(!profiler_entries)
    return;
  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = get_cpu_id();
  uint64_t now = get_now();
  size_t type_size = sizeof(instant_colored_entry_t);

  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);

  instant_colored_entry_t* entry = (instant_colored_entry_t*)(profiler_entries + entry_idx);
  entry->header.type = EVENT_TYPE_INSTANT_COLORED;
  entry->header.cpu_id = cpu_id;
  entry->header.task_id = task_id;
  entry->time_stamp_begin = now;
  entry->name = name;
  entry->color = color;

  if (link_out) {
    if (*link_out == 0) {
      ENTER_CRITICAL(&link_index_mutex);
      //critical section
        *link_out = ++link_index;
      EXIT_CRITICAL(&link_index_mutex);
    }
  }

  if (link_in) {
    insert_link_event(link_in, LINK_TYPE_IN, now, cpu_id, task_id);
  }
  if (link_out && *link_out) {
    insert_link_event(*link_out, LINK_TYPE_OUT, now, cpu_id, task_id);
  }
}

void trace_counter(const char* name, int32_t value, uint8_t color) {
  if(!profiler_entries)
    return;
  uint8_t task_id = get_current_task_id();
  uint8_t cpu_id = get_cpu_id();
  uint64_t now = get_now();
  size_t type_size = sizeof(counter_entry_t);

  size_t entry_idx = 0;
  advance_pointers(type_size, &entry_idx);

  counter_entry_t* entry = (counter_entry_t*)(profiler_entries + entry_idx);
  entry->header.type = EVENT_TYPE_COUNTER;
  entry->header.cpu_id = cpu_id;
  entry->header.task_id = task_id;
  entry->time_stamp_begin = now;
  entry->name = name;
  entry->value = value;
}