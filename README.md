# MabuTrace

MabuTrace is a lightweight, C and C++ compatible high-performance tracing library for the ESP32, designed for deep performance analysis and debugging of complex, real-time applications. It captures execution data and exports it in a JSON format compatible with standard profiling tools like [Perfetto](https://ui.perfetto.dev) and the legacy `chrome://tracing`.

The library is written to be highly efficient, using a binary circular buffer to minimize runtime overhead. This makes it safe to use in timing-critical code, including Interrupt Service Routines (ISRs). It works seamlessly with both the **ESP-IDF** and the **Arduino framework**.

## Key Features

-   **Low Overhead:** Events are stored in a compact binary format in a pre-allocated circular buffer, ensuring minimal impact on application performance.
-   **ISR-Safe:** Tracepoints can be safely added within Interrupt Service Routines.
-   **Built-in Web Server:** Starts an HTTP server on the ESP32 to serve a web-based UI for capturing, downloading, and directly opening traces in Perfetto.
-   **Simple API:** Provides intuitive macros for instrumenting your code (`TRACE_SCOPE`, `TRACE_INSTANT`, `TRACE_FLOW_IN`/`_OUT`, `TRACE_COUNTER`).
-   **Rich Event Types:**
    -   **Scoped Events:** Measure the duration of a function or code block.
    -   **Instant Events:** Mark a specific point in time.
    -   **Counter Events:** Track the value of a variable over time.
    -   **Flow Events:** Visualize causal relationships between tasks, even across core and interrupt boundaries.
-   **Framework Agnostic:** Full support for both ESP-IDF and Arduino projects.

## Getting Started

### Installation

#### Arduino (PlatformIO)
Add MabuTrace as a library dependency in your `platformio.ini`:
```ini
lib_deps =
    https://github.com/mabuware/MabuTrace.git
```
Or, place the library in your project's `lib/` directory.

#### Arduino (IDE)
1.  Download this repository as a ZIP file.
2.  In the Arduino IDE, go to `Sketch` -> `Include Library` -> `Add .ZIP Library...`.
3.  Select the downloaded ZIP file.

#### ESP-IDF

**1. Using Component Manager (Recommended)**

Add the following to your project's `idf_component.yml` manifest:

```yaml
dependencies:
  mabutrace:
    git: https://github.com/mabuware/MabuTrace.git
    version: "*"
```

**2. Manual Clone**

Alternatively, clone the repository into your `components` directory:

```sh
cd your-project/components
git clone https://github.com/mabuware/MabuTrace.git
```

### Basic Usage

1.  **Include the library:**
    ```cpp
    #include <mabutrace.h>
    ```

2.  **Initialize the tracer:** In your `setup()` function (Arduino) or `app_main()` (ESP-IDF), initialize the library. It's also recommended to start the web server for easy trace retrieval.
    ```cpp
    #include <WiFi.h> // Or your ESP-IDF WiFi equivalent

    void setup() {
        // ... connect to WiFi ...
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
        }

        // Initialize MabuTrace and start the web server
        mabutrace_init();
        mabutrace_start_server(81); // Use any available port

        Serial.print("MabuTrace server started. Go to http://");
        Serial.print(WiFi.localIP());
        Serial.println(":81/ to capture a trace.");
    }
    ```

3.  **Add tracepoints to your code:** Use the macros to instrument your application logic. (See API examples below).

4.  **Capture a Trace:**
    -   Run the firmware on your ESP32 and connect to its IP address (e.g., `http://192.168.1.123:81`).
    -   You will see the MabuTrace web UI.
    -   Click **"Capture Trace"** to download the latest trace data from the ESP32's buffer. Note that clicking will **end** the trace, as it is already being continuously written to the ring buffer.
    -   Once complete, you can:
        -   **"Save Trace"** to download the `trace.json` file locally. Then open `chrome://tracing` and load the file. (Chrome only)
        -   **"Open Trace in Perfetto"** to automatically open a new tab and load the trace data for analysis. (Any browser)

## API by Example

MabuTrace provides simple macros that make instrumenting your code easy. **Note:** Event names are passed as `const char*` and are not copied. For this reason, you should **only use string literals** for names to ensure the pointers remain valid.

### Scoped Duration Events

Use `TRACE_SCOPE` to measure how long a block of code takes. It automatically records the start time when created and the end time when it goes out of scope.

```cpp
void process_data() {
    // This trace will cover the entire duration of the process_data function.
    TRACE_SCOPE("process_data", COLOR_GREEN);

    {
        // You can create nested scopes.
        TRACE_SCOPE("sub-process A");
        delay(10);
    }

    delay(5);
}
```

For convenience, `TRC()` is a shortcut for `TRACE_SCOPE(__func__)`, using the current function's name.

```cpp
void my_worker_function() {
    TRC(); // Equivalent to TRACE_SCOPE("my_worker_function");
    // ...
}
```

### Instant Events

Use `TRACE_INSTANT` to mark a single point in time, such as an error condition or an important event.

```cpp
if (xQueueSend(myQueue, &data, 0) == errQUEUE_FULL) {
    // Mark that the queue was full.
    TRACE_INSTANT("Queue Full", COLOR_DARK_RED);
}
```

### Flow Events

Flow events are used to visualize the cause-and-effect relationship between events in different threads or contexts (e.g., from an ISR to a worker task).

1.  Create an outbound flow event with `TRACE_FLOW_OUT`, passing a pointer to a `uint16_t` variable which will be filled with a link ID.
2.  Pass this link ID along with your data (e.g., in a queue message).
3.  In the receiving task, create an inbound flow event with `TRACE_FLOW_IN` using the received ID.

Perfetto and chrome://tracing will draw a connecting arrow from the outbound event to the inbound one.

```cpp
// In an ISR or Task A (Producer)
void onTimer() {
    TRC();
    message_t message;

    // Create a link ID for the flow. MabuTrace will assign a unique ID.
    uint16_t link_idx = 0;
    TRACE_FLOW_OUT(&link_idx, "New Data");
    message.link = link_idx; // Store the ID in the message

    xQueueSendFromISR(Queue1, &message, &xHigherPriorityTaskWoken);
}


// In Task B (Consumer)
void workerTask(void *pvParameters) {
    for (;;) {
        message_t message;
        xQueueReceive(Queue1, &message, portMAX_DELAY);

        // This links back to the TRACE_FLOW_OUT event in the ISR.
        TRACE_FLOW_IN(message.link);

        // ... process message ...
    }
}
```

### Counter Events

Use `TRACE_COUNTER` to track the value of a variable over time. Perfetto will render this as a graph. Note that counters are stored as 24bit signed integer in the binary buffer, meaning it's possible to trace values between -8388608 and 8388607.

```cpp
void monitor_task() {
    for (;;) {
        // Track the number of messages waiting in a queue.
        int free_heap = esp_get_free_heap_size();
        TRACE_COUNTER("Free Heap", free_heap);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

## How It Works

1.  **Binary Logging:** The `TRACE_` macros are lightweight functions that write event data into a compact binary struct. This struct is then copied into a global circular buffer. Access to the buffer is protected by a critical section (`portMUX_TYPE`) to ensure thread and ISR safety.
2.  **Circular Buffer:** When the buffer fills up, it wraps around, overwriting the oldest entries. This ensures the tracer can run indefinitely without ever running out of memory.
3.  **On-the-fly JSON Conversion:** The web server does **not** pre-allocate a massive buffer for the JSON output. Instead, it reads the binary data from the circular buffer and converts each entry to a JSON string one by one, streaming the result to the client. This keeps memory usage low and constant.
4.  **Task Naming:** The library keeps track of FreeRTOS `TaskHandle_t`s and automatically associates them with task names for clear labeling in the trace viewer.

## License

MabuTrace is free software, distributed under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.