#ifdef ESP32
  #include "esp32.h"
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
  #include "win.h"
#endif