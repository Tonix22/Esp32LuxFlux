#pragma once
#include <cstdint>

namespace luxflux {
struct LedTiming {
    const char *name;
    std::uint32_t t0h_ns;
    std::uint32_t t0l_ns;
    std::uint32_t t1h_ns;
    std::uint32_t t1l_ns;
    std::uint32_t reset_us;
};
LedTiming selectedLedTiming();
}
