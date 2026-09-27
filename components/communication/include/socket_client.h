#pragma once

#include <cstddef>
#include <cstdint>
#include "communication_types.h"

namespace luxflux {
class SocketClient {
public:
    SocketClient() = default;
    ~SocketClient();
    SocketClient(const SocketClient &) = delete;
    SocketClient &operator=(const SocketClient &) = delete;
    esp_err_t connect(const char *host, std::uint16_t port,
                      std::uint32_t timeout_ms = kDefaultSocketTimeoutMs);
    SocketResult send(const std::uint8_t *data, std::size_t length);
    SocketResult receive(std::uint8_t *buffer, std::size_t capacity);
    void close();
    int release() { const int fd = fd_; fd_ = -1; return fd; }
    bool connected() const { return fd_ >= 0; }
private:
    int fd_ = -1;
};
}
