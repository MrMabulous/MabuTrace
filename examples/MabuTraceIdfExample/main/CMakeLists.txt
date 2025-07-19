idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS ".")

set(REQUIRES
    mabutrace
    esp_wifi
    esp_event
    esp_netif
    nvs_flash
    driver
    esp_common
    esp_system
    freertos
)

target_link_libraries(${COMPONENT_TARGET} PRIVATE ${REQUIRES})