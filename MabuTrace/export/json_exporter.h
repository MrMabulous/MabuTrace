#ifndef __MABUTRACE_ESP32_H__
#define __MABUTRACE_ESP32_H__

#include <string>

std::string get_json_trace();
bool write_to_file(std::string file_path);

#endif  // __MABUTRACE_ESP32_H__