config_setting(
    name = "windows",
    constraint_values = [
        "@bazel_tools//platforms:windows",
    ],
)

cc_library(
    name = "mabu_trace",
    srcs = select({
        "windows": [
            "export/json_exporter.cc",
            "platform/win.cc",
        ],
    }) + [],
    hdrs = [
        "export/json_exporter.h",
        "mabutrace.h",
        "platform/platform.h",
        "platform/win.h",
    ],
    visibility = ["//visibility:public"],
    copts = select({
        "windows": [
            "/DPROFILER_BUFFER_SIZE_IN_BYTES=536870912", #512MB
        ],
    }),
)

cc_test(
    name = "mabu_trace_test",
    size = "small",
    srcs = [
        "test/mabutrace_test.cc",
    ],
    deps = [
        ":mabu_trace",
        "@gtest//:main",
    ],
)