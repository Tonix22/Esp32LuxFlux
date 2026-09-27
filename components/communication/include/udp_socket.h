#pragma once

#include <cstddef>
#include <cstdint>
#include "communication_types.h"

namespace luxflux {
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket &) = delete;
    UdpSocket &operator=(const UdpSocket &) = delete;
    esp_err_t open(std::uint16_t local_port = 0,
                   std::uint32_t timeout_ms = kDefaultSocketTimeoutMs);
    SocketResult sendTo(const char *host, std::uint16_t port,
                        const std::uint8_t *data, std::size_t length);
    SocketResult receiveFrom(std::uint8_t *buffer, std::size_t capacity);
    void close();
private:
    int fd_ = -1;
};
}
