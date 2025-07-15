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

#ifndef __MABUTRACE_H__
#define __MABUTRACE_H__

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
* Size of the circular buffer.
*/
//#define USE_PSRAM_IF_AVAILABLE
#define PROFILER_BUFFER_SIZE_IN_BYTES 65536 // 64kb

/*
* Predefined colors.
*/
#define COLOR_UNDEFINED      0x00  /* Let the visualizer choose a color */
#define COLOR_GREEN          0x01
#define COLOR_LIGHT_GREEN    0x02    
#define COLOR_DARK_ORANGE    0x03
#define COLOR_DARK_RED       0x04
#define COLOR_YELLOW         0x05
#define COLOR_OLIVE          0x06
#define COLOR_BLACK          0x07
#define COLOR_WHITE          0x08
#define COLOR_GRAY           0x09
#define COLOR_LIGHT_GRAY     0x0A

/*
* The following macros should be used for tracing ([] denotes optional argument).
* Note that name is NOT copied, meaning that the pointer should remain valid. For this reason it's recommended
* that the macros are only used with string literals as name.
*
* TRC();
* TRACE_SCOPE(const char* name, [uint8_t color]);
* TRACE_SCOPE_LINKED(const char* name, uint16_t link_in, uint16_t* link_out, [uint8_t color]);
* TRACE_INSTANT(const char* name, [uint8_t color]);
* TRACE_COUNTER(const char* name, int24_t value, [uint8_t color]);
*/

#define _OVERLOAD_MACRO(_1,_2,_3, _4, NAME,...) NAME

#define TRACE_SCOPE(...) _OVERLOAD_MACRO(__VA_ARGS__, 0, 0, _TRACE_SCOPE_COLORED, _TRACE_SCOPE_UNCOLORED)(__VA_ARGS__)
#define TRC() TRACE_SCOPE(__func__)
#define TRACE_SCOPE_LINKED(...) _OVERLOAD_MACRO(__VA_ARGS__, _TRACE_SCOPE_LINKED_COLORED, _TRACE_SCOPE_LINKED_UNCOLORED, 0, 0)(__VA_ARGS__)
#define TRACE_INSTANT(...) _OVERLOAD_MACRO(__VA_ARGS__, 0, 0, _TRACE_INSTANT_COLORED, _TRACE_INSTANT_UNCOLORED)(__VA_ARGS__)
#define TRACE_COUNTER(...) _OVERLOAD_MACRO(__VA_ARGS__, 0, _TRACE_COUNTER_COLORED, _TRACE_COUNTER_UNCOLORED, 0)(__VA_ARGS__)

#define _TRACE_INSTANT_UNCOLORED(name) trace_instant(name, COLOR_UNDEFINED);
#define _TRACE_INSTANT_COLORED(name, color) trace_instant(name, color);
#define _TRACE_INSTANT_LINKED(name, link_in, link_out, color) trace_instant(name, link_in, link_out, color);
#define _TRACE_COUNTER_UNCOLORED(name, value) trace_counter(name, value, COLOR_UNDEFINED);
#define _TRACE_COUNTER_COLORED(name, value, color) trace_counter(name, value, color);

#ifdef __cplusplus
#define _TRACE_SCOPE_UNCOLORED(name) Profiler scope_trace_helper_object(name, COLOR_UNDEFINED);
#define _TRACE_SCOPE_COLORED(name, color) Profiler scope_trace_helper_object(name, color);
#define _TRACE_SCOPE_LINKED_UNCOLORED(name, link_in, link_out) Profiler scope_trace_helper_object(name, link_in, link_out, COLOR_UNDEFINED);
#define _TRACE_SCOPE_LINKED_COLORED(name, link_in, link_out, color) Profiler scope_trace_helper_object(name, link_in, link_out, color);
extern "C" {
#else
#define _TRACE_SCOPE_UNCOLORED(name) profiler_duration_handle_t scope_trace_helper_handle __attribute__ ((__cleanup__(trace_end))) = trace_begin(name, COLOR_UNDEFINED);
#define _TRACE_SCOPE_COLORED(name, color) profiler_duration_handle_t scope_trace_helper_handle __attribute__ ((__cleanup__(trace_end))) = trace_begin(name, color);
#define _TRACE_SCOPE_LINKED_UNCOLORED(name, link_in, link_out) profiler_duration_handle_t scope_trace_helper_handle __attribute__ ((__cleanup__(trace_end))) = trace_begin_linked(name, link_in, link_out, COLOR_UNDEFINED);
#define _TRACE_SCOPE_LINKED_COLORED(name, link_in, link_out, color) profiler_duration_handle_t scope_trace_helper_handle __attribute__ ((__cleanup__(trace_end))) = trace_begin_linked(name, link_in, link_out, color);
#endif

typedef struct {
  uint64_t time_stamp_begin_microseconds;
  const char* name;
  uint16_t link_in;
  uint16_t link_out;
  uint8_t color;
} profiler_duration_handle_t;

typedef struct {
  uint8_t type : 3;  // 2^3 = 8 different event types.
  uint8_t cpu_id : 1;  // 2 cpus.
  uint8_t task_id : 4;  // 2^4 = 16 different tasks.
} __attribute__((packed)) entry_header_t;
#define EVENT_TYPE_NONE 0

typedef struct {
  entry_header_t header;
  unsigned int time_duration_microseconds : 24;  // Duration of event in microseconds. 24bit yields up to 16 seconds duration.
  uint32_t time_stamp_begin_microseconds;  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  const char* name;  // Name of the event.
} __attribute__((packed)) duration_entry_t;
#define EVENT_TYPE_DURATION 1

typedef struct {
  entry_header_t header;
  uint8_t color;
  unsigned int time_duration_microseconds;  // Duration of event in microseconds.
  uint32_t time_stamp_begin_microseconds;  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  const char* name;  // Name of the event.
} __attribute__((packed)) duration_colored_entry_t;
#define EVENT_TYPE_DURATION_COLORED 2

typedef struct {
  entry_header_t header;
  uint8_t color;
  uint32_t time_stamp_begin_microseconds;  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  const char* name;  // Name of the event.
} __attribute__((packed)) instant_colored_entry_t;
#define EVENT_TYPE_INSTANT_COLORED 3

typedef struct {
  entry_header_t header;
  signed int value : 24;  // 24 bits allows for values between -8388608 and 8388607
  uint32_t time_stamp_begin_microseconds;  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  const char* name;  // Name of the event.
} __attribute__((packed)) counter_entry_t;
#define EVENT_TYPE_COUNTER 4

typedef struct {
  entry_header_t header;
  uint8_t link_type;  // 0: in, 1: out
  uint16_t link;  // Link id
  uint32_t time_stamp_begin_microseconds;  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
} __attribute__((packed)) link_entry_t;
#define EVENT_TYPE_LINK 5
#define LINK_TYPE_IN 0
#define LINK_TYPE_OUT 1

typedef struct {
  uint8_t type;  // Type of event. Based on this type, different fields from the union part are valid.
  uint8_t cpu_id;  // ID of CPU from which event was traced.
  uint8_t color;  // One of a few predefined color values.
  uint16_t link_in;  // Flow Event id's to visualize links between events.
  uint16_t link_out;
  void* task_handle;  // FreeRTOS task handle. NULL if called from interrupt.
  const char* name;  // Name of the event.
  uint32_t time_stamp_begin_microseconds; // Start of event start in microseconds since device started.
  union {
    struct {  // EVENT_TYPE_DURATION additional fields.
      uint32_t time_duration_microseconds; // Duration of event in microseconds.
    };
    struct {  // EVENT_TYPE_COUNTER additional fields.
      int32_t counter_value;  // Value of the counter to keep track of.
    };
  };
} profiler_entry_t;

void mabutrace_init();
void mabutrace_deinit();
esp_err_t mabutrace_start_server(int port);
size_t get_json_size();
void get_json_trace(char* json_buffer, size_t json_buffer_size);
void get_json_trace_chunked(void* ctx, void (*process_chunk)(void*, const char*, size_t));

size_t get_smallest_type_size();
size_t get_buffer_size();
void profiler_get_entries(void* output_buffer, size_t* out_start_idx, size_t* out_end_idx);
size_t get_num_task_handles();
void profiler_get_task_handles(void* output_taskhandle_16);
profiler_duration_handle_t trace_begin(const char* name, uint8_t color);
profiler_duration_handle_t trace_begin_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color);
void trace_end(profiler_duration_handle_t* handle);
void trace_instant(const char* name, uint8_t color);
void trace_instant_linked(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color);
void trace_counter(const char* name, int32_t value, uint8_t color);

#ifdef __cplusplus
class Profiler {
public:
  Profiler(const char* name, uint8_t color) { _handle = trace_begin(name, color); }
  Profiler(const char* name, uint16_t link_in, uint16_t* link_out, uint8_t color) { _handle = trace_begin_linked(name, link_in, link_out, color); }
  ~Profiler() { trace_end(&_handle); }
private:
  profiler_duration_handle_t _handle;
};
}
#endif

#endif  //__MABUTRACE_H__