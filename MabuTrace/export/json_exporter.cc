#include "json_exporter.h"

#include "../mabutrace.h"

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <fstream>
#include <iostream>

// An upper bound estimate for the number of chars required for the json output of each entry
#define MAX_CHARS_PER_ENTRY 256

std::string get_json_trace() {
  char* profiler_entries;
  size_t profiler_buffer_size = get_buffer_size();
  profiler_entries = new char[profiler_buffer_size];

  size_t start_idx;
  size_t end_idx;
  profiler_get_entries(profiler_entries, &start_idx, &end_idx);

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

  static const char* json_header = "{\n"
                                   "  \"traceEvents\": [\n";
  static const char* json_footer = "    {}\n"
                                   "  ],\n"
                                   "  \"displayTimeUnit\": \"ms\",\n"
                                   "  \"otherData\": {\n"
                                   "    \"version\": \"Mabutrace Profiler v1.0\"\n"
                                   "    \"timer_freq:\": %lu\n"
                                   "    \"digits\": %d\n"
                                   "  }\n"
                                   "}";

  static const size_t header_and_footer_bytes = 256; // rounded up

  // compute conservative buffer size.
  size_t min_type_size = get_smallest_type_size();
  size_t max_number_elements = profiler_buffer_size / min_type_size;
  size_t json_buffer_size = header_and_footer_bytes + max_number_elements * MAX_CHARS_PER_ENTRY;
  size_t timestamp_frequency = get_timestamp_frequency();  // unit of timestamps is seconds / timestamp_frequency.
  double timestamp_frequency_mult = 1000000.0/static_cast<long double>(timestamp_frequency);  // multiplier to convert to microsecond fractionals.
  int number_of_digits = std::max(0, static_cast<int>(std::log10(timestamp_frequency / 1000000.0) + 0.5));

  char* json_buffer = new char[json_buffer_size];

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

    std::string threadNameStr = std::string("Unnamed_") + std::to_string(entry_header->task_id);
    //char* threadName = pcTaskGetTaskName(task_handles[entry_header->task_id]);
    const char* threadName = (entry_header->task_id == 0) ? (char*)"INTERRUPT" : threadNameStr.c_str();
    size_t entry_size;
    switch (entry_header->type) {
      case EVENT_TYPE_DURATION: {
        duration_entry_t* entry = (duration_entry_t*)entry_header;
        entry_size = sizeof(duration_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":\"%s\",\"ts\":%.*f,\"dur\":%.*f,\"args\":{\"cpu\":%d}},\n",
                             entry->name, threadName, number_of_digits, entry->time_stamp_begin * timestamp_frequency_mult, number_of_digits, entry->time_duration * timestamp_frequency_mult, (int)entry_header->cpu_id);
        break;
      }
      case EVENT_TYPE_DURATION_COLORED: {
        duration_colored_entry_t* entry = (duration_colored_entry_t*)entry_header;
        entry_size = sizeof(duration_colored_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"X\",\"pid\":0,\"tid\":\"%s\",\"ts\":%.*f,\"dur\":%.*f,\"args\":{\"cpu\":%d}%s},\n",
                             entry->name, threadName, number_of_digits, entry->time_stamp_begin * timestamp_frequency_mult, number_of_digits, entry->time_duration * timestamp_frequency_mult, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_INSTANT_COLORED: {
        instant_colored_entry_t* entry = (instant_colored_entry_t*)entry_header;
        entry_size = sizeof(instant_colored_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"i\",\"pid\":0,\"tid\":\"%s\",\"ts\":%.*f,\"s\":\"p\",\"args\":{\"cpu\":%d}%s},\n",
                             entry->name, threadName, number_of_digits, entry->time_stamp_begin * timestamp_frequency_mult, (int)entry_header->cpu_id, colorNameLookup[entry->color]);
        break;
      }
      case EVENT_TYPE_COUNTER: {
        counter_entry_t* entry = (counter_entry_t*)entry_header;
        entry_size = sizeof(counter_entry_t);
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"%s\",\"ph\":\"C\",\"pid\":0,\"tid\":\"%s\",\"ts\":%.*f,\"args\":{\"value\":%d}},\n",
                             entry->name, threadName, number_of_digits, entry->time_stamp_begin * timestamp_frequency_mult, (int)entry->value);
        break;
      }
      case EVENT_TYPE_LINK: {
        link_entry_t* entry = (link_entry_t*)entry_header;
        entry_size = sizeof(link_entry_t);
        char phase = (entry->link_type == LINK_TYPE_IN) ? 'f' : 's';
        lineLength = sprintf(chunk + ofst, "    {\"name\":\"flow\",\"cat\":\"flow\",\"id\":%u,\"ph\":\"%c\",\"pid\":0,\"tid\":\"%s\",\"ts\":%.*f},\n",
                            (unsigned int)entry->link, phase, threadName, number_of_digits, entry->time_stamp_begin * timestamp_frequency_mult); 
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
    assert(("Buffer overflow.", ofst <= json_buffer_size));

    // advance idx
    idx += entry_size;
    if(idx >= PROFILER_BUFFER_SIZE_IN_BYTES) {
      loopCount++;
      idx = 0;
    }
  } while (idx != end_idx && loopCount <= 1);

  lineLength = sprintf(chunk + ofst, json_footer, (unsigned long)timestamp_frequency, number_of_digits);

  std::string result(json_buffer);
  delete[] json_buffer;
  delete[] profiler_entries;

  return result;
}

bool write_to_file(std::string file_path) {
  std::ofstream out(file_path);
  out << get_json_trace();
  out.close();
  return true;
}