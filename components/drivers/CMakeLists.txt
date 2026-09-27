idf_component_register(SRCS "wifi_driver.cpp" INCLUDE_DIRS "include"
    REQUIRES config esp_common log esp_wifi esp_netif esp_event nvs_flash freertos)
