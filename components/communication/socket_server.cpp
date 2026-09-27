#include "socket_server.h"
#include "esp_log.h"
#include <cerrno>
#include <lwip/sockets.h>

namespace luxflux {
static constexpr const char *TAG = "socket_server";
SocketServer::~SocketServer() { close(); }
void SocketServer::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

esp_err_t SocketServer::listen(std::uint16_t port, int backlog)
{
    if (port == 0 || backlog < 1) return ESP_ERR_INVALID_ARG;
    close();
    int candidate = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (candidate < 0) return ESP_FAIL;
    int reuse = 1;
    setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(candidate, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        ::listen(candidate, backlog) != 0) {
        ESP_LOGW(TAG, "Listen failed: errno=%d", errno);
        ::close(candidate);
        return ESP_FAIL;
    }
    fd_ = candidate;
    return ESP_OK;
}

esp_err_t SocketServer::accept(int &client_fd, std::uint32_t timeout_ms)
{
    client_fd = -1;
    if (fd_ < 0) return ESP_ERR_INVALID_STATE;
    if (timeout_ms == 0) return ESP_ERR_INVALID_ARG;
    struct timeval timeout = {static_cast<time_t>(timeout_ms / 1000),
                              static_cast<suseconds_t>((timeout_ms % 1000) * 1000)};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    client_fd = ::accept(fd_, nullptr, nullptr);
    return client_fd >= 0 ? ESP_OK : ESP_FAIL;
}
}
