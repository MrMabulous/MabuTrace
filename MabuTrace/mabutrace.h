#ifndef __MABUTRACE_H__
#define __MABUTRACE_H__

#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

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
* TRACE_SCOPE(const char* name, [uint8_t color]);
* TRACE_SCOPE_LINKED(const char* name, uint16_t link_in, uint16_t* link_out, [uint8_t color]);
* TRACE_INSTANT(const char* name, [uint8_t color]);
* TRACE_COUNTER(const char* name, int24_t value, [uint8_t color]);
*/

#define EXPAND( x ) x  // required for propper expansion on MSVC
#define _OVERLOAD_MACRO(_1,_2,_3,_4,NAME,...) NAME

#ifdef DISABLE_MABUTRACE_MACROS
  #define TRACE_SCOPE(...) do {} while(0)
  #define TRACE_SCOPE_LINKED(...) do {} while(0)
  #define TRACE_INSTANT(...) do {} while(0)
  #define TRACE_COUNTER(...) do {} while(0)
#else
  #define TRACE_SCOPE(...) EXPAND( _OVERLOAD_MACRO(__VA_ARGS__, 0, 0, _TRACE_SCOPE_COLORED, _TRACE_SCOPE_UNCOLORED)(__VA_ARGS__) )
  #define TRACE_SCOPE_LINKED(...) EXPAND( _OVERLOAD_MACRO(__VA_ARGS__, _TRACE_SCOPE_LINKED_COLORED, _TRACE_SCOPE_LINKED_UNCOLORED, 0, 0)(__VA_ARGS__) )
  #define TRACE_INSTANT(...) EXPAND( _OVERLOAD_MACRO(__VA_ARGS__, 0, 0, _TRACE_INSTANT_COLORED, _TRACE_INSTANT_UNCOLORED)(__VA_ARGS__) )
  #define TRACE_COUNTER(...) EXPAND( _OVERLOAD_MACRO(__VA_ARGS__, 0, _TRACE_COUNTER_COLORED, _TRACE_COUNTER_UNCOLORED, 0)(__VA_ARGS__) )
#endif

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

#define EVENT_TYPE_NONE 0
#define EVENT_TYPE_DURATION 1
#define EVENT_TYPE_DURATION_COLORED 2
#define EVENT_TYPE_INSTANT_COLORED 3
#define EVENT_TYPE_COUNTER 4
#define EVENT_TYPE_LINK 5
#define LINK_TYPE_IN 0
#define LINK_TYPE_OUT 1

void profiler_init(); // uses default size
void profiler_init_with_size(size_t ring_buffer_size_in_bytes);
void profiler_deinit();
size_t get_smallest_type_size();
size_t get_buffer_size();
size_t get_timestamp_frequency(); // return number of timestamp increments per second.
void profiler_get_entries(void* output_buffer, size_t* out_start_idx, size_t* out_end_idx);
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

}  // extern C
#endif

#endif  //__MABUTRACE_H__