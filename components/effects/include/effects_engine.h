#pragma once
#include "led_driver.h"
#include "rgb_sequence.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace luxflux {
class EffectsEngine {
public:
    explicit EffectsEngine(LedDriver &driver) : driver_(driver) {}
    ~EffectsEngine();
    esp_err_t start();
    void notify(SyncProgress event);
    esp_err_t activate(const LightSequence &sequence);
private:
    static void taskEntry(void *argument);
    void taskLoop();
    void solid(Rgb color);
    bool render(const LightFrame &frame);
    LedDriver &driver_;
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    LightSequence pending_;
    bool has_pending_ = false;
};
}
