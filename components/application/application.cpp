#include "application.h"
#include "board_config.h"
#include "diagnostic_patterns.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "sync_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

namespace luxflux 
{
    static constexpr const char *TAG = "application";

    // The constructor initializer list passes the config component's limits to SyncService.
    Application::Application() : effects_(leds_), sync_(wifi_, discovery_, effects_, config::makeLimits()) {}

    esp_err_t Application::initialize()
    {
        if (board::kLedDataGpio < 0) 
        {
            ESP_LOGI(TAG, "LED output disabled until a board GPIO is configured");
        } 
        else 
        {
            esp_err_t result = leds_.initialize(board::kLedDataGpio, CONFIG_LUXFLUX_LED_COUNT, CONFIG_LUXFLUX_LED_BRIGHTNESS_LIMIT);
            if (result != ESP_OK)
            {
                return result;
            } 
            #if CONFIG_LUXFLUX_LED_DIAGNOSTIC_ON_BOOT
            result = runLedDiagnostics(leds_);
            if (result != ESP_OK) return result;
            #endif
        }
        esp_err_t result = effects_.start();
        if (result != ESP_OK) 
            return result;
        result = sync_.restoreSavedSequence();
        if (result != ESP_OK && result != ESP_ERR_NOT_FOUND)
            ESP_LOGW(TAG, "Saved sequence unavailable: %s", esp_err_to_name(result));
    #if CONFIG_LUXFLUX_SYNC_SERVER || CONFIG_LUXFLUX_SYNC_CLIENT
        if (!CONFIG_LUXFLUX_SYNC_WIFI_SSID[0]) 
        {
            ESP_LOGE(TAG, "Synchronization Wi-Fi SSID is empty");
            return ESP_ERR_INVALID_ARG;
        }
        result = wifi_.initialize();
        if (result != ESP_OK) return result;
    #if CONFIG_LUXFLUX_SYNC_SERVER
        // This ESP32 acts as a SoftAP server with a built-in demo sequence.
        LightSequence demo;
        demo.frames.push_back({{{static_cast<std::uint16_t>(CONFIG_LUXFLUX_LED_COUNT), {255, 0, 0}}}, 250});
        demo.frames.push_back({{{static_cast<std::uint16_t>(CONFIG_LUXFLUX_LED_COUNT), {0, 255, 0}}}, 250});
        if (!sync_.store().find(CONFIG_LUXFLUX_SYNC_SEQUENCE_NAME) &&
            sync_.store().replace(CONFIG_LUXFLUX_SYNC_SEQUENCE_NAME, std::move(demo)) != SyncStatus::Ok)
            return ESP_ERR_INVALID_ARG;
        result = sync_.startServer({CONFIG_LUXFLUX_SYNC_WIFI_SSID,
                                    CONFIG_LUXFLUX_SYNC_WIFI_PASSWORD, 1, 4});
    #else
        // In the current test build, this ESP32 is the client of the Python server.
        result = sync_.startClient({CONFIG_LUXFLUX_SYNC_WIFI_SSID,
                                    CONFIG_LUXFLUX_SYNC_WIFI_PASSWORD},
                                CONFIG_LUXFLUX_SYNC_SEQUENCE_NAME,
                                CONFIG_LUXFLUX_SYNC_SERVER_HOST_OVERRIDE);
    #endif
        if (result != ESP_OK) return result;
    #endif
        return ESP_OK;
    }

    void Application::run()
    {
        ESP_LOGI(TAG, "Infrastructure ready; application behavior is deferred");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
