file(GLOB SOURCES
    src/*.c
    src/*.cc
    src/*.cpp
)

idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS "include"
        REQUIRES esp_timer esp_http_server
)