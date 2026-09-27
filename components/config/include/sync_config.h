#pragma once

#include "rgb_sequence.h"

namespace luxflux::config {
// Convert this firmware build's ESP-IDF settings into the portable limits
// object used by SyncService, SyncProtocol, and SequenceStore.
SyncLimits makeLimits();
}
