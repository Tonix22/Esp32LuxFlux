#include "udp_socket.h"
#include "common_config.h"
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <cstdio>

namespace luxflux {
UdpSocket::~UdpSocket() { close(); }
void UdpSocket::close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

esp_err_t UdpSocket::open(std::uint16_t local_port, std::uint32_t timeout_ms)
{
    if (timeout_ms == 0) return ESP_ERR_INVALID_ARG;
    close();
    int candidate = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (candidate < 0) return ESP_FAIL;
    struct timeval timeout = {static_cast<time_t>(timeout_ms / 1000),
                              static_cast<suseconds_t>((timeout_ms % 1000) * 1000)};
    setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(local_port);
    if (::bind(candidate, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        ::close(candidate);
        return ESP_FAIL;
    }
    fd_ = candidate;
    return ESP_OK;
}

SocketResult UdpSocket::sendTo(const char *host, std::uint16_t port,
                               const std::uint8_t *data, std::size_t length)
{
    if (fd_ < 0) return {ESP_ERR_INVALID_STATE, 0};
    if (!host || port == 0 || (!data && length) || length > kMaximumSocketBufferBytes)
        return {ESP_ERR_INVALID_ARG, 0};
    char service[6];
    snprintf(service, sizeof(service), "%u", static_cast<unsigned>(port));
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *address = nullptr;
    if (getaddrinfo(host, service, &hints, &address) != 0) return {ESP_FAIL, 0};
    const auto result = ::sendto(fd_, data, length, 0, address->ai_addr, address->ai_addrlen);
    freeaddrinfo(address);
    return result < 0 ? SocketResult{ESP_FAIL, 0} : SocketResult{ESP_OK, static_cast<std::size_t>(result)};
}

SocketResult UdpSocket::receiveFrom(std::uint8_t *buffer, std::size_t capacity)
{
    if (fd_ < 0) return {ESP_ERR_INVALID_STATE, 0};
    if (!buffer || capacity == 0 || capacity > kMaximumSocketBufferBytes) return {ESP_ERR_INVALID_ARG, 0};
    const auto result = ::recvfrom(fd_, buffer, capacity, 0, nullptr, nullptr);
    return result < 0 ? SocketResult{ESP_FAIL, 0} : SocketResult{ESP_OK, static_cast<std::size_t>(result)};
}
}
