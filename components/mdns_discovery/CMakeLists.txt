idf_component_register(SRCS "mdns_discovery.cpp" "enumeration.cpp"
    INCLUDE_DIRS "include" REQUIRES mdns esp_common esp_event esp_hw_support
    esp_wifi drivers freertos log)
