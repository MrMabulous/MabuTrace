file(GLOB SOURCES
    src/*.c
    src/*.cc
)

idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "src"
        REQUIRES esp_timer esp_http_server
)

idf_build_set_property(COMPILE_OPTIONS "-include${CMAKE_CURRENT_SOURCE_DIR}/src/mabutrace_hooks.h" APPEND)