#ifndef __MABUTRACE_EXPORT_H__
#define __MABUTRACE_EXPORT_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t get_json_size();
void get_json_trace(char* json_buffer, size_t json_buffer_size);
void get_json_trace_chunked(void* ctx, void (*process_chunk)(void*, const char*, size_t));

#ifdef __cplusplus
}
#endif

#endif