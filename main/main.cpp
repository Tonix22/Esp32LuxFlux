#include "application.h"
#include "common_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

static constexpr const char *TAG = "main";

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Starting %s %s on %s", luxflux::kFirmwareName,
             luxflux::kFirmwareVersion, CONFIG_IDF_TARGET);

    luxflux::Application application;
    const esp_err_t result = application.initialize();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Application initialization failed: %s", esp_err_to_name(result));
        return;
    }
    application.run();
}
