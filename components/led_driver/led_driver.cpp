#include "led_driver.h"
#include "led_protocol.h"
#include "common_config.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <algorithm>

namespace luxflux {
static constexpr const char *TAG = "led_driver";
static constexpr std::uint32_t kResolutionHz = 10000000;

static std::uint16_t ticks(std::uint32_t nanoseconds)
{
    return static_cast<std::uint16_t>((nanoseconds + 50) / 100);
}

LedDriver::~LedDriver() { shutdown(); }

esp_err_t LedDriver::initialize(int gpio, std::size_t count, std::uint8_t brightness_limit)
{
    if (ready()) return ESP_ERR_INVALID_STATE;
    if (gpio < 0 || gpio > 21 || count == 0 || count > kMaximumLedCount || brightness_limit == 0)
        return ESP_ERR_INVALID_ARG;
    // selectedLedTiming() comes from the protocol chosen in Kconfig.
    const LedTiming timing = selectedLedTiming();
    if (!ticks(timing.t0h_ns) || !ticks(timing.t0l_ns) ||
        !ticks(timing.t1h_ns) || !ticks(timing.t1l_ns)) return ESP_ERR_INVALID_ARG;

    rmt_tx_channel_config_t channel_config = {};
    channel_config.gpio_num = static_cast<gpio_num_t>(gpio);
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = kResolutionHz;
    channel_config.mem_block_symbols = 48;
    channel_config.trans_queue_depth = 4;
    esp_err_t result = rmt_new_tx_channel(&channel_config, &channel_);
    if (result != ESP_OK) return result;

    rmt_bytes_encoder_config_t encoder_config = {};
    encoder_config.bit0.level0 = 1;
    encoder_config.bit0.duration0 = ticks(timing.t0h_ns);
    encoder_config.bit0.level1 = 0;
    encoder_config.bit0.duration1 = ticks(timing.t0l_ns);
    encoder_config.bit1.level0 = 1;
    encoder_config.bit1.duration0 = ticks(timing.t1h_ns);
    encoder_config.bit1.level1 = 0;
    encoder_config.bit1.duration1 = ticks(timing.t1l_ns);
    encoder_config.flags.msb_first = 1;
    result = rmt_new_bytes_encoder(&encoder_config, &encoder_);
    if (result != ESP_OK) { shutdown(); return result; }
    result = rmt_enable(channel_);
    if (result != ESP_OK) { shutdown(); return result; }
    frame_.resize(count * 3, 0);
    count_ = count;
    brightness_limit_ = brightness_limit;
    reset_us_ = timing.reset_us;
    ESP_LOGI(TAG, "%s selected; %u LEDs on GPIO %d", timing.name,
             static_cast<unsigned>(count), gpio);
    return clear();
}

esp_err_t LedDriver::show(const Rgb *pixels, std::size_t count)
{
    if (!ready() || !encoder_) return ESP_ERR_INVALID_STATE;
    if (!pixels || count != count_) return ESP_ERR_INVALID_ARG;
    // Scale logical RGB colors to the brightness limit, then pack the GRB
    // wire order used by the current LED output path (including WS2812B).
    for (std::size_t i = 0; i < count_; ++i) {
        const auto scale = [this](std::uint8_t value) -> std::uint8_t {
            return static_cast<std::uint8_t>((static_cast<unsigned>(value) * brightness_limit_) / 255);
        };
        frame_[3 * i] = scale(pixels[i].green);
        frame_[3 * i + 1] = scale(pixels[i].red);
        frame_[3 * i + 2] = scale(pixels[i].blue);
    }
    rmt_transmit_config_t tx_config = {};
    // RMT converts the packed bytes to timed pulses on the configured GPIO.
    esp_err_t result = rmt_transmit(channel_, encoder_, frame_.data(), frame_.size(), &tx_config);
    if (result != ESP_OK) return result;
    result = rmt_tx_wait_all_done(channel_, 1000);
    if (result == ESP_OK) esp_rom_delay_us(reset_us_);
    return result;
}

esp_err_t LedDriver::clear()
{
    if (!ready() || !encoder_) return ESP_ERR_INVALID_STATE;
    std::fill(frame_.begin(), frame_.end(), 0);
    rmt_transmit_config_t tx_config = {};
    esp_err_t result = rmt_transmit(channel_, encoder_, frame_.data(), frame_.size(), &tx_config);
    if (result != ESP_OK) return result;
    result = rmt_tx_wait_all_done(channel_, 1000);
    if (result == ESP_OK) esp_rom_delay_us(reset_us_);
    return result;
}

void LedDriver::shutdown()
{
    if (channel_ && encoder_) clear();
    if (channel_) { rmt_disable(channel_); rmt_del_channel(channel_); channel_ = nullptr; }
    if (encoder_) { rmt_del_encoder(encoder_); encoder_ = nullptr; }
    frame_.clear();
    count_ = 0;
}
}
