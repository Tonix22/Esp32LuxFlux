#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "driver/rmt_tx.h"
#include "esp_err.h"

namespace luxflux {
struct Rgb { std::uint8_t red = 0, green = 0, blue = 0; };

class LedDriver {
public:
    LedDriver() = default;
    ~LedDriver();
    LedDriver(const LedDriver &) = delete;
    LedDriver &operator=(const LedDriver &) = delete;
    esp_err_t initialize(int gpio, std::size_t count, std::uint8_t brightness_limit);
    esp_err_t show(const Rgb *pixels, std::size_t count);
    esp_err_t clear();
    void shutdown();
    bool ready() const { return channel_ != nullptr; }
    std::size_t count() const { return count_; }

private:
    rmt_channel_handle_t channel_ = nullptr;
    rmt_encoder_handle_t encoder_ = nullptr;
    std::size_t count_ = 0;
    std::uint8_t brightness_limit_ = 0;
    std::uint32_t reset_us_ = 80;
    std::vector<std::uint8_t> frame_;
};
}
