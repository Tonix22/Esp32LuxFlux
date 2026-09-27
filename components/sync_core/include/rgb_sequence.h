#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace luxflux {
struct RgbColor { std::uint8_t red, green, blue; };
struct PixelGroup { std::uint16_t pixel_count; RgbColor color; };
struct LightFrame { std::vector<PixelGroup> groups; std::uint32_t duration_ms; };
struct LightSequence { std::vector<LightFrame> frames; };

// Shared validation rules for one SyncProtocol/SequenceStore instance. These
// member values are defaults for callers such as host tests. The config
// component replaces them with ESP-IDF values for the running firmware.
struct SyncLimits {
    std::size_t led_count = 16;
    std::size_t max_sequences = 4;
    std::size_t max_frames_per_sequence = 128;
    std::size_t max_groups_per_frame = 256;
    std::size_t max_payload_length = 1024;
    std::size_t max_total_bytes = 16384;
    std::uint32_t min_duration_ms = 1;
    std::uint32_t max_duration_ms = 10000;
    unsigned max_timeout_retries = 2;
};

enum class SyncStatus {
    Ok, Timeout, Disconnected, IoError, Malformed, LimitExceeded,
    UnexpectedMessage, NotFound, InvalidState
};

inline const char *syncStatusName(SyncStatus status)
{
    switch (status) {
    case SyncStatus::Ok: return "ok";
    case SyncStatus::Timeout: return "timeout";
    case SyncStatus::Disconnected: return "disconnected";
    case SyncStatus::IoError: return "socket I/O error";
    case SyncStatus::Malformed: return "malformed frame or sequence";
    case SyncStatus::LimitExceeded: return "configured limit exceeded";
    case SyncStatus::UnexpectedMessage: return "unexpected control message";
    case SyncStatus::NotFound: return "sequence not found";
    case SyncStatus::InvalidState: return "invalid state";
    }
    return "unknown";
}

enum class LightSyncEvent {
    LoadStarted, TransferStarted, FrameReceived, ProgressUpdated,
    TransferCompleted, TransferFailed, SequenceActivated
};

struct SyncProgress { LightSyncEvent event; std::uint32_t completed; std::uint32_t total; };

bool validateSequence(const LightSequence &sequence, const SyncLimits &limits);
std::size_t estimatedSequenceBytes(const LightSequence &sequence);
}
