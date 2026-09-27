set(led_backend "")
if(CONFIG_LUXFLUX_LED_WS2812B)
    set(led_backend "drivers/ws2812b.cpp")
elseif(CONFIG_LUXFLUX_LED_SK6812)
    set(led_backend "drivers/sk6812.cpp")
elseif(CONFIG_LUXFLUX_LED_WS2811_800)
    set(led_backend "drivers/ws2811_800.cpp")
elseif(CONFIG_LUXFLUX_LED_WS2811_400)
    set(led_backend "drivers/ws2811_400.cpp")
elseif(CONFIG_LUXFLUX_LED_UCS1903)
    set(led_backend "drivers/ucs1903.cpp")
elseif(CONFIG_LUXFLUX_LED_SM16703)
    set(led_backend "drivers/sm16703.cpp")
elseif(CONFIG_LUXFLUX_LED_GENERIC)
    set(led_backend "drivers/generic_rmt.cpp")
else()
    # ESP-IDF's first dependency-discovery pass runs before sdkconfig is loaded.
    set(led_backend "drivers/ws2812b.cpp")
endif()

idf_component_register(SRCS "led_driver.cpp" "led_driver_factory.cpp" "diagnostic_patterns.cpp" ${led_backend}
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "private_include" "drivers"
    REQUIRES config esp_driver_rmt esp_common log esp_rom freertos)
