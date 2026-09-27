#pragma once

#include <cstdint>
#include <string>
#include "esp_err.h"
#include "rgb_sequence.h"

namespace luxflux::nvm {

// Values that a future NVM backend could restore before LedDriver starts.
// Zero count/brightness and GPIO -1 mean no configuration has been loaded.
struct LedConfiguration {
    std::uint16_t led_count = 0;
    int data_gpio = -1;
    std::uint8_t brightness_limit = 0;
};

class NvmStore {
public:
    // Skeleton API: neither method accesses flash yet. Both return
    // ESP_ERR_NOT_SUPPORTED until a storage backend is implemented.
    esp_err_t saveLedConfiguration(const LedConfiguration &configuration);
    esp_err_t loadLedConfiguration(LedConfiguration &configuration);
    // Persist only the active, fully validated sequence. A failed load leaves
    // the output arguments untouched.
    esp_err_t saveSequence(const std::string &name, const LightSequence &sequence,
                           const SyncLimits &limits);
    esp_err_t loadSequence(std::string &name, LightSequence &sequence,
                           const SyncLimits &limits);
};

} // namespace luxflux::nvm
