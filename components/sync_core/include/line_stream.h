#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include "rgb_sequence.h"

namespace luxflux {
enum class IoState { Data, Timeout, Disconnected, Error };
struct IoResult { IoState state; std::size_t bytes; };

class ByteStream {
public:
    virtual ~ByteStream() = default;
    virtual IoResult receive(std::uint8_t *buffer, std::size_t capacity) = 0;
    virtual IoResult send(const std::uint8_t *data, std::size_t length) = 0;
};

class LineStream {
public:
    LineStream(ByteStream &stream, const SyncLimits &limits) : stream_(stream), limits_(limits) {}
    SyncStatus read(std::string &line);
    SyncStatus write(std::string_view line);
    SyncStatus feed(const std::uint8_t *bytes, std::size_t length);
private:
    ByteStream &stream_;
    const SyncLimits &limits_;
    std::string partial_;
    std::deque<std::string> ready_;
};
}
