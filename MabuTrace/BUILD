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
            "platform/win.cc",
        ],
    }) + [],
    hdrs = [
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