#include "effects_engine.h"
#include "esp_log.h"
#include <vector>

namespace luxflux {
static constexpr const char *TAG = "effects";
EffectsEngine::~EffectsEngine()
{
    if (task_) vTaskDelete(task_);
    if (queue_) vQueueDelete(queue_);
    if (mutex_) vSemaphoreDelete(mutex_);
}

esp_err_t EffectsEngine::start()
{
    if (task_) return ESP_ERR_INVALID_STATE;
    queue_ = xQueueCreate(16, sizeof(SyncProgress));
    mutex_ = xSemaphoreCreateMutex();
    if (!queue_ || !mutex_) return ESP_ERR_NO_MEM;
    if (xTaskCreate(taskEntry, "luxflux_effects", 6144, this, 4, &task_) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void EffectsEngine::notify(SyncProgress event)
{
    if (queue_ && xQueueSend(queue_, &event, pdMS_TO_TICKS(50)) != pdTRUE)
        ESP_LOGW(TAG, "Synchronization event queue full");
}

esp_err_t EffectsEngine::activate(const LightSequence &sequence)
{
    // Stage a copy for the effects task; only that task changes playback state.
    if (!mutex_) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    pending_ = sequence;
    has_pending_ = true;
    xSemaphoreGive(mutex_);
    return ESP_OK;
}

void EffectsEngine::taskEntry(void *argument)
{ static_cast<EffectsEngine *>(argument)->taskLoop(); }

void EffectsEngine::solid(Rgb color)
{
    if (!driver_.ready()) return;
    std::vector<Rgb> pixels(driver_.count(), color);
    driver_.show(pixels.data(), pixels.size());
}

bool EffectsEngine::render(const LightFrame &frame)
{
    if (!driver_.ready()) return false;
    std::vector<Rgb> pixels(driver_.count());
    std::size_t at = 0;
    // Expand a group such as 4(255,0,0) into four individual red pixels.
    for (const auto &group : frame.groups)
        for (std::uint16_t i = 0; i < group.pixel_count && at < pixels.size(); ++i)
            pixels[at++] = {group.color.red, group.color.green, group.color.blue};
    return at == pixels.size() && driver_.show(pixels.data(), pixels.size()) == ESP_OK;
}

void EffectsEngine::taskLoop()
{
    LightSequence playing;
    std::size_t frame_index = 0;
    bool reported_output = false;
    TickType_t next_frame = xTaskGetTickCount();
    for (;;) {
        SyncProgress event = {};
        if (xQueueReceive(queue_, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
            switch (event.event) {
            case LightSyncEvent::LoadStarted:
                solid({0, 0, 255});
                break;
            case LightSyncEvent::TransferStarted:
                solid({0, 0, 64});
                break;
            case LightSyncEvent::FrameReceived:
            case LightSyncEvent::ProgressUpdated:
                if (driver_.ready() && driver_.count()) {
                    std::vector<Rgb> pixels(driver_.count());
                    const std::size_t index = event.completed % pixels.size();
                    pixels[index] = {0, 0, 255};
                    driver_.show(pixels.data(), pixels.size());
                }
                break;
            case LightSyncEvent::TransferCompleted:
                if (driver_.ready()) driver_.clear();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case LightSyncEvent::TransferFailed:
                solid({255, 0, 0});
                vTaskDelay(pdMS_TO_TICKS(200));
                next_frame = xTaskGetTickCount();
                break;
            case LightSyncEvent::SequenceActivated:
                // Replace the old playback sequence after the complete
                // transfer, starting again at frame zero.
                if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (has_pending_) {
                        playing.frames.swap(pending_.frames);
                        pending_.frames.clear();
                        has_pending_ = false;
                        frame_index = 0;
                        reported_output = false;
                    }
                    xSemaphoreGive(mutex_);
                }
                solid({0, 255, 0});
                vTaskDelay(pdMS_TO_TICKS(150));
                next_frame = xTaskGetTickCount();
                break;
            }
        }
        // Keep cycling through the frames using each frame's duration_ms.
        if (!playing.frames.empty() && static_cast<std::int32_t>(xTaskGetTickCount() - next_frame) >= 0) {
            const auto &frame = playing.frames[frame_index];
            const bool output_sent = render(frame);
            if (!reported_output) {
                if (output_sent) ESP_LOGI(TAG, "Complete frame sent to generic LED driver");
                else ESP_LOGW(TAG, "Sequence active; LED output disabled or failed");
                reported_output = true;
            }
            next_frame = xTaskGetTickCount() + pdMS_TO_TICKS(frame.duration_ms);
            frame_index = (frame_index + 1) % playing.frames.size();
        }
    }
}
}
