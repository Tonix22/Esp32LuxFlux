#include "diagnostic_patterns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <vector>

namespace luxflux {
esp_err_t runLedDiagnostics(LedDriver &driver)
{
    if (!driver.ready()) return ESP_ERR_INVALID_STATE;
    std::vector<Rgb> pixels(driver.count());
    const Rgb colors[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    esp_err_t result = ESP_OK;
    for (const auto &color : colors) {
        for (auto &pixel : pixels) pixel = color;
        result = driver.show(pixels.data(), pixels.size());
        if (result != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (result == ESP_OK) {
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            for (auto &pixel : pixels) pixel = {};
            pixels[i] = {255, 255, 255};
            result = driver.show(pixels.data(), pixels.size());
            if (result != ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(40));
        }
    }
    const esp_err_t clear_result = driver.clear();
    return result == ESP_OK ? clear_result : result;
}
}
