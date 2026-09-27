idf_component_register(SRCS "effects_engine.cpp" INCLUDE_DIRS "include"
    REQUIRES led_driver sync_core config freertos esp_common log)
