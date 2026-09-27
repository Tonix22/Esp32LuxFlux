#pragma once
#include <string>
#include <string_view>
#include "rgb_sequence.h"

namespace luxflux {
SyncStatus parseLegacyFrame(std::string_view text, const SyncLimits &limits, LightFrame &out);
SyncStatus formatLegacyFrame(const LightFrame &frame, const SyncLimits &limits,
                             std::string &out);
}
