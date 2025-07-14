#ifndef __MABUTRACE_SERVER_H__
#define __MABUTRACE_SERVER_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the web server
esp_err_t start_mabutrace_server(int port);

#ifdef __cplusplus
}
#endif

#endif