idf_component_register(SRCS "nvm_store.cpp" INCLUDE_DIRS "include"
    REQUIRES esp_common nvs_flash sync_core)
