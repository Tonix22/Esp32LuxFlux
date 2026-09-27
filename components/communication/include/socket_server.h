#pragma once

#include <cstdint>
#include "communication_types.h"

namespace luxflux {
class SocketServer {
public:
    SocketServer() = default;
    ~SocketServer();
    SocketServer(const SocketServer &) = delete;
    SocketServer &operator=(const SocketServer &) = delete;
    esp_err_t listen(std::uint16_t port, int backlog = 2);
    esp_err_t accept(int &client_fd, std::uint32_t timeout_ms = kDefaultSocketTimeoutMs);
    void close();
    bool listening() const { return fd_ >= 0; }
private:
    int fd_ = -1;
};
}
