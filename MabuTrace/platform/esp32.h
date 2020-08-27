#ifndef __MABUTRACE_ESP32_H__
#define __MABUTRACE_ESP32_H__

/*
* Size of the circular buffer.
*/
#define USE_PSRAM_IF_AVAILABLE
#define PROFILER_BUFFER_SIZE_IN_BYTES 65536 // 64kb

typedef struct {
  uint64_t time_stamp_begin_microseconds;
  const char* name;
  uint16_t link_in;
  uint16_t link_out;
  uint8_t color; 
} profiler_duration_handle_t;

// Header contained in each packed event. 8 bit size.
typedef struct {
  // 2^3 = 8 different event types.
  uint8_t type : 3;
  // 2 cpus.
  uint8_t cpu_id : 1;
  // 2^4 = 16 different tasks.
  uint8_t task_id : 4;
} __attribute__((packed)) entry_header_t;

// EVENT_TYPE_DURATION
typedef struct {
  entry_header_t header;
  // Duration of event in microseconds. 24bit yields up to 16 seconds duration.
  unsigned int time_duration_microseconds : 24;
  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  uint32_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
} __attribute__((packed)) duration_entry_t;

// EVENT_TYPE_DURATION_COLORED
typedef struct {
  entry_header_t header;
  uint8_t color;
  // Duration of event in microseconds.
  uint32_t time_duration_microseconds;
  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  uint32_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
} __attribute__((packed)) duration_colored_entry_t;

// EVENT_TYPE_INSTANT_COLORED
typedef struct {
  entry_header_t header;
  uint8_t color;
  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  uint32_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
} __attribute__((packed)) instant_colored_entry_t;

// EVENT_TYPE_COUNTER
typedef struct {
  entry_header_t header;
  // 24 bits allows for values between -8388608 and 8388607
  signed int value : 24;
  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  uint32_t time_stamp_begin_microseconds;
  // Name of the event.
  const char* name;
} __attribute__((packed)) counter_entry_t;

// EVENT_TYPE_LINK
typedef struct {
  entry_header_t header;
  // 0: in, 1: out
  uint8_t link_type;
  // Link id
  uint16_t link;
  // Start of event start in microseconds since device started. 32bit overflows every 70 minutes.
  uint32_t time_stamp_begin_microseconds;
} __attribute__((packed)) link_entry_t;

#endif  // __MABUTRACE_ESP32_H__
