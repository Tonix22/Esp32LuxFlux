#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace luxflux {
enum class Transport { TCP, UDP };
struct SocketResult {
    esp_err_t status = ESP_OK;
    std::size_t bytes = 0;
};
inline constexpr std::uint32_t kDefaultSocketTimeoutMs = 1000;
}
