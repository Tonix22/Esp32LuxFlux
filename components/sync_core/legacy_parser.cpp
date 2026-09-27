#include "legacy_parser.h"
#include <limits>

namespace luxflux {
namespace {
bool number(std::string_view s, std::size_t &at, std::uint32_t maximum, std::uint32_t &out)
{
    if (at >= s.size() || s[at] < '0' || s[at] > '9') return false;
    std::uint32_t value = 0;
    do {
        const unsigned digit = static_cast<unsigned>(s[at++] - '0');
        if (digit > maximum || value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    } while (at < s.size() && s[at] >= '0' && s[at] <= '9');
    out = value;
    return true;
}
bool take(std::string_view s, std::size_t &at, char expected)
{
    if (at >= s.size() || s[at] != expected) return false;
    ++at;
    return true;
}
}

SyncStatus parseLegacyFrame(std::string_view text, const SyncLimits &limits, LightFrame &out)
{
    while (!text.empty() && (text.back() == '\0' || text.back() == '\n' || text.back() == '\r'))
        text.remove_suffix(1);
    if (text.empty() || text.size() > limits.max_payload_length ||
        limits.led_count == 0 || limits.led_count > UINT16_MAX)
        return SyncStatus::LimitExceeded;
    LightFrame candidate;
    candidate.duration_ms = 0;
    std::size_t at = 0;
    std::size_t pixels = 0;
    while (at < text.size() && candidate.groups.size() < limits.max_groups_per_frame) {
        std::uint32_t count, red, green, blue;
        if (!number(text, at, UINT16_MAX, count) || count == 0 || !take(text, at, '(') ||
            !number(text, at, 255, red) || !take(text, at, ',') ||
            !number(text, at, 255, green) || !take(text, at, ',') ||
            !number(text, at, 255, blue) || !take(text, at, ')') ||
            !take(text, at, ',')) return SyncStatus::Malformed;
        if (count > limits.led_count - pixels) return SyncStatus::Malformed;
        pixels += count;
        candidate.groups.push_back({static_cast<std::uint16_t>(count),
                                    {static_cast<std::uint8_t>(red),
                                     static_cast<std::uint8_t>(green),
                                     static_cast<std::uint8_t>(blue)}});
        if (pixels == limits.led_count) {
            std::uint32_t duration;
            if (!number(text, at, limits.max_duration_ms, duration) ||
                duration < limits.min_duration_ms || at != text.size()) return SyncStatus::Malformed;
            candidate.duration_ms = duration;
            out = std::move(candidate);
            return SyncStatus::Ok;
        }
    }
    return SyncStatus::Malformed;
}

SyncStatus formatLegacyFrame(const LightFrame &frame, const SyncLimits &limits, std::string &out)
{
    LightSequence single;
    single.frames.push_back(frame);
    if (!validateSequence(single, limits)) return SyncStatus::Malformed;
    std::string candidate;
    for (const auto &group : frame.groups) {
        candidate += std::to_string(group.pixel_count) + "(" +
                     std::to_string(group.color.red) + "," +
                     std::to_string(group.color.green) + "," +
                     std::to_string(group.color.blue) + "),";
    }
    candidate += std::to_string(frame.duration_ms);
    if (candidate.size() > limits.max_payload_length) return SyncStatus::LimitExceeded;
    out.swap(candidate);
    return SyncStatus::Ok;
}

std::size_t estimatedSequenceBytes(const LightSequence &sequence)
{
    std::size_t bytes = sequence.frames.size() * sizeof(LightFrame);
    for (const auto &frame : sequence.frames) bytes += frame.groups.size() * sizeof(PixelGroup);
    return bytes;
}

bool validateSequence(const LightSequence &sequence, const SyncLimits &limits)
{
    if (sequence.frames.empty() || sequence.frames.size() > limits.max_frames_per_sequence ||
        limits.led_count == 0 || limits.led_count > UINT16_MAX) return false;
    std::size_t bytes = sequence.frames.size() * sizeof(LightFrame);
    for (const auto &frame : sequence.frames) {
        if (frame.groups.empty() || frame.groups.size() > limits.max_groups_per_frame ||
            frame.duration_ms < limits.min_duration_ms || frame.duration_ms > limits.max_duration_ms)
            return false;
        if (bytes > limits.max_total_bytes ||
            frame.groups.size() > (limits.max_total_bytes - bytes) / sizeof(PixelGroup)) return false;
        bytes += frame.groups.size() * sizeof(PixelGroup);
        std::size_t pixels = 0;
        for (const auto &group : frame.groups) {
            if (group.pixel_count == 0 || group.pixel_count > limits.led_count - pixels) return false;
            pixels += group.pixel_count;
        }
        if (pixels != limits.led_count) return false;
    }
    return bytes <= limits.max_total_bytes;
}
}
