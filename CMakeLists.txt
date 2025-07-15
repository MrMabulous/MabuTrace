file(GLOB SOURCES
    src/*.c
    src/*.cc
)

idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "src"
        REQUIRES esp_timer esp_http_server
)