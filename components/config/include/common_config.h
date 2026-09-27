#pragma once

#include <cstddef>
#include <cstdint>

namespace luxflux {
inline constexpr const char *kFirmwareName = "LuxFlux";
inline constexpr const char *kFirmwareVersion = "0.1.0";
inline constexpr std::size_t kDefaultTaskStackBytes = 4096;
inline constexpr std::size_t kMaximumSocketBufferBytes = 4096;
inline constexpr std::size_t kMaximumLedCount = 256;
}
