idf_component_register(SRCS "sync_service.cpp" INCLUDE_DIRS "include"
    REQUIRES sync_core drivers communication effects config nvm mdns_discovery esp_common log lwip freertos)
