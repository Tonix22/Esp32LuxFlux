#include "socket_client.h"
#include "common_config.h"
#include "esp_log.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

namespace luxflux {
static constexpr const char *TAG = "socket_client";
SocketClient::~SocketClient() { close(); }
void SocketClient::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

esp_err_t SocketClient::connect(const char *host, std::uint16_t port, std::uint32_t timeout_ms)
{
    if (!host || port == 0 || timeout_ms == 0) return ESP_ERR_INVALID_ARG;
    close();
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char service[6];
    snprintf(service, sizeof(service), "%u", static_cast<unsigned>(port));
    struct addrinfo *addresses = nullptr;
    if (getaddrinfo(host, service, &hints, &addresses) != 0) return ESP_FAIL;
    esp_err_t result = ESP_FAIL;
    for (auto *address = addresses; address; address = address->ai_next) {
        int candidate = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate < 0) continue;
        struct timeval timeout = {static_cast<time_t>(timeout_ms / 1000),
                                  static_cast<suseconds_t>((timeout_ms % 1000) * 1000)};
        setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (::connect(candidate, address->ai_addr, address->ai_addrlen) == 0) {
            fd_ = candidate;
            result = ESP_OK;
            break;
        }
        ESP_LOGW(TAG, "Connect failed: errno=%d", errno);
        ::close(candidate);
    }
    freeaddrinfo(addresses);
    return result;
}

SocketResult SocketClient::send(const std::uint8_t *data, std::size_t length)
{
    if (fd_ < 0) return {ESP_ERR_INVALID_STATE, 0};
    if ((!data && length) || length > kMaximumSocketBufferBytes) return {ESP_ERR_INVALID_ARG, 0};
    const auto result = ::send(fd_, data, length, 0);
    return result < 0 ? SocketResult{ESP_FAIL, 0} : SocketResult{ESP_OK, static_cast<std::size_t>(result)};
}

SocketResult SocketClient::receive(std::uint8_t *buffer, std::size_t capacity)
{
    if (fd_ < 0) return {ESP_ERR_INVALID_STATE, 0};
    if (!buffer || capacity == 0 || capacity > kMaximumSocketBufferBytes) return {ESP_ERR_INVALID_ARG, 0};
    const auto result = ::recv(fd_, buffer, capacity, 0);
    if (result == 0) { close(); return {ESP_ERR_INVALID_STATE, 0}; }
    return result < 0 ? SocketResult{ESP_FAIL, 0} : SocketResult{ESP_OK, static_cast<std::size_t>(result)};
}
}
