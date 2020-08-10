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

typedef struct {
  // Type of event. Based on this type, different fields from the union part are valid.
  uint8_t type;
  // ID of CPU from which event was traced.
  uint8_t cpu_id;
  // One of a few predefined color values.
  uint8_t color;
  // Flow Event id's to visualize links between events.
  uint16_t link_in;
  uint16_t link_out;
  // FreeRTOS task handle. NULL if called from interrupt.
  void* task_handle;
  // Name of the event.
  const char* name;
  // Start of event start in microseconds since device started.
  uint32_t time_stamp_begin_microseconds;
  union {
    struct {
	  // EVENT_TYPE_DURATION additional fields.
	  // Duration of event in microseconds.
      uint32_t time_duration_microseconds;
    };
    struct {
	  // EVENT_TYPE_COUNTER additional fields.
	  // Value of the counter to keep track of.
      int32_t counter_value;
    };
  };
} profiler_entry_t;

#endif  // __MABUTRACE_ESP32_H__
