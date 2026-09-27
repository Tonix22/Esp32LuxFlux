idf_component_register(SRCS "application.cpp" INCLUDE_DIRS "include"
    REQUIRES config drivers communication led_driver effects sync_service mdns_discovery esp_common log freertos)
