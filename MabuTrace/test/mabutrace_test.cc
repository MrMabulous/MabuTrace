// #define DISABLE_MABUTRACE_MACROS

#include "../mabutrace.h"
#include "../export/json_exporter.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <vector>

void Swap(float *xp, float *yp) 
{
  TRACE_SCOPE("Swap()", COLOR_YELLOW);
  float temp = *xp; 
  *xp = *yp; 
  *yp = temp; 
} 
  
// A bublesort algorithm for testing purposes.
void BubbleSort(std::vector<float>& data) {
  TRACE_SCOPE("BubbleSort()", COLOR_DARK_RED);
  int n = data.size();
  int i, j; 
  bool swapped; 
  for (i = 0; i < n-1; i++) {
    TRACE_SCOPE("BubbleSort outer loop", COLOR_GREEN);
    swapped = false; 
    for (j = 0; j < n-i-1; j++) {
      TRACE_SCOPE("BubbleSort inner loop", COLOR_OLIVE);
      if (data[j] > data[j+1]) {
          Swap(&data[j], &data[j+1]);
          swapped = true; 
      }
    }
    // IF no two elements were swapped by inner loop, then break 
    if (swapped == false) 
      break; 
  } 
}

void Randomize(std::vector<float>& data) {
  TRACE_SCOPE("Randomize()");
  std::default_random_engine generator;
  std::uniform_real_distribution<float> distribution(-1.0f,1.0f);
  for(int i=0; i<data.size(); i++) {
    data[i] = distribution(generator);
  }
}

void Scramble(std::vector<float>& data) {
  TRACE_SCOPE("Scramble()", COLOR_LIGHT_GRAY);
  auto rng = std::default_random_engine {};
  std::shuffle(std::begin(data), std::end(data), rng);
}

void Sort(std::vector<float>& data) {
  TRACE_SCOPE("Sort()", COLOR_LIGHT_GREEN);
  std::sort(data.begin(), data.end());
}

TEST(MabuTraceTest, LockUnlock) {
  size_t buffer_size = 16*1024*1024; // 16MB
  profiler_init_with_size(buffer_size);
  ASSERT_TRUE(buffer_size == get_buffer_size());
  {
    TRACE_SCOPE("TEST");
    std::vector<float> test_vec(1000);
    Randomize(test_vec);
    Sort(test_vec);
    Scramble(test_vec);
    BubbleSort(test_vec);
  }
  write_to_file("C:/deleteme/tracetest.json");
}