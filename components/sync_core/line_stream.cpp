#include "line_stream.h"

namespace luxflux {
// TCP provides arbitrary byte chunks, not complete messages. Keep bytes in
// partial_ until a newline (or legacy NUL) ends a line, then queue that line.
SyncStatus LineStream::feed(const std::uint8_t *bytes, std::size_t length)
{
    if (!bytes && length) return SyncStatus::Malformed;
    for (std::size_t i = 0; i < length; ++i) {
        char ch = static_cast<char>(bytes[i]);
        if (ch == '\n' || ch == '\0') {
            if (!partial_.empty() && partial_.back() == '\r') partial_.pop_back();
            if (!partial_.empty()) ready_.push_back(std::move(partial_));
            partial_.clear();
            if (ready_.size() > limits_.max_frames_per_sequence + 4)
                return SyncStatus::LimitExceeded;
        } else {
            if (partial_.size() >= limits_.max_payload_length) return SyncStatus::LimitExceeded;
            partial_.push_back(ch);
        }
    }
    return SyncStatus::Ok;
}

SyncStatus LineStream::read(std::string &line)
{
    unsigned timeouts = 0;
    // One receive may contain half a line or several lines; feed() handles both.
    while (ready_.empty()) {
        std::uint8_t buffer[256];
        const IoResult result = stream_.receive(buffer, sizeof(buffer));
        if (result.state == IoState::Timeout) {
            if (++timeouts > limits_.max_timeout_retries) return SyncStatus::Timeout;
            continue;
        }
        if (result.state == IoState::Disconnected) return SyncStatus::Disconnected;
        if (result.state != IoState::Data || result.bytes == 0 || result.bytes > sizeof(buffer))
            return SyncStatus::IoError;
        const SyncStatus fed = feed(buffer, result.bytes);
        if (fed != SyncStatus::Ok) return fed;
    }
    line = std::move(ready_.front());
    ready_.pop_front();
    return SyncStatus::Ok;
}

SyncStatus LineStream::write(std::string_view line)
{
    if (line.size() > limits_.max_payload_length) return SyncStatus::LimitExceeded;
    std::string data(line);
    data.push_back('\n');
    // A socket send may write only part of the line, so continue from sent.
    std::size_t sent = 0;
    unsigned timeouts = 0;
    while (sent < data.size()) {
        const IoResult result = stream_.send(reinterpret_cast<const std::uint8_t *>(data.data() + sent),
                                             data.size() - sent);
        if (result.state == IoState::Timeout) {
            if (++timeouts > limits_.max_timeout_retries) return SyncStatus::Timeout;
            continue;
        }
        if (result.state == IoState::Disconnected) return SyncStatus::Disconnected;
        if (result.state != IoState::Data || result.bytes == 0 || result.bytes > data.size() - sent)
            return SyncStatus::IoError;
        sent += result.bytes;
    }
    return SyncStatus::Ok;
}
}
