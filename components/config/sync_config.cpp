#include "sync_config.h"
#include "sdkconfig.h"

namespace luxflux::config {
// Kconfig defines the menu entries; ESP-IDF generates CONFIG_* values from
// the local sdkconfig. The type SyncLimits does not depend on ESP-IDF.
SyncLimits makeLimits()
{
    SyncLimits limits; // Starts with the defaults in rgb_sequence.h.
    // These assignments change fields of this local object, not the struct
    // definition. The running board uses CONFIG_LUXFLUX_LED_COUNT (9 in the
    // current test profile).
    limits.led_count = CONFIG_LUXFLUX_LED_COUNT;
    limits.max_sequences = CONFIG_LUXFLUX_SYNC_MAX_SEQUENCES;
    limits.max_frames_per_sequence = CONFIG_LUXFLUX_SYNC_MAX_FRAMES;
    limits.max_groups_per_frame = CONFIG_LUXFLUX_SYNC_MAX_GROUPS;
    limits.max_payload_length = CONFIG_LUXFLUX_SYNC_MAX_PAYLOAD;
    limits.max_total_bytes = CONFIG_LUXFLUX_SYNC_MAX_MEMORY;
    limits.min_duration_ms = CONFIG_LUXFLUX_SYNC_MIN_DURATION_MS;
    limits.max_duration_ms = CONFIG_LUXFLUX_SYNC_MAX_DURATION_MS;
    limits.max_timeout_retries = CONFIG_LUXFLUX_SYNC_TIMEOUT_RETRIES;
    return limits;
}
}
