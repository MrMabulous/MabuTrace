#ifndef __MABUTRACE_WIN_H__
#define __MABUTRACE_WIN_H__

typedef struct {
  uint64_t time_stamp_begin_microseconds;
  const char* name;
  uint16_t link_in;
  uint16_t link_out;
  uint8_t color; 
} profiler_duration_handle_t;

// Header contained in each packed event. 16 bit size.
#pragma pack(push,1)
typedef struct {
  // 2^3 = 8 different event types.
  uint8_t type : 3;
  // 2^6 = 64 cpus.
  uint8_t cpu_id : 6;
  // 2^7 = 128 different threads.
  uint8_t task_id : 4;
} entry_header_t;
#pragma pack(pop)
#define MAX_THREAD_ID 127
#define MAX_CPU_ID 63

// EVENT_TYPE_DURATION
#pragma pack(push,1)
typedef struct {
  entry_header_t header;
  // Duration of event in microseconds. 32bit yields up to 70 minutes duration.
  uint32_t time_duration_microseconds;
  // Start of event start in microseconds since device started.
  uint64_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
} duration_entry_t;
#pragma pack(pop)

// EVENT_TYPE_DURATION_COLORED
#pragma pack(push,1)
typedef struct {
  entry_header_t header;
  uint8_t color;
  // Duration of event in microseconds.
  uint32_t time_duration_microseconds;
  // Start of event start in microseconds since device started.
  uint64_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
} duration_colored_entry_t;
#pragma pack(pop)

// EVENT_TYPE_INSTANT_COLORED
#pragma pack(push,1)
typedef struct {
  entry_header_t header;
  uint8_t color;
  // Start of event start in microseconds since device started.
  uint64_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
} instant_colored_entry_t;
#pragma pack(pop)

// EVENT_TYPE_COUNTER
#pragma pack(push,1)
typedef struct {
  entry_header_t header;
  // 32 bits allows for values between -2,147,483,648 and +2,147,483,647
  int32_t value;
  // Start of event start in microseconds since device started.
  uint64_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
}  counter_entry_t;
#pragma pack(pop)

// EVENT_TYPE_LINK
#pragma pack(push,1)
typedef struct {
  entry_header_t header;
  // 0: in, 1: out
  uint8_t link_type;
  // Link id
  uint16_t link;
  // Start of event start in microseconds since device started.
  uint64_t time_stamp_begin_microseconds;
} link_entry_t;
#pragma pack(pop)

#endif  // __MABUTRACE_WIN_H__
