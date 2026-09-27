#include "sync_service.h"
#include "socket_client.h"
#include "socket_server.h"
#include "esp_log.h"
#include <cerrno>
#include <cstring>
#include <lwip/sockets.h>

namespace luxflux {
static constexpr const char *TAG = "rgb_sync";
static constexpr std::uint16_t kPort = 3333;

// Adapt a TCP socket to the byte-stream interface used by portable sync_core.
// LineStream handles message boundaries above this raw recv/send layer.
class SocketStream final : public ByteStream {
public:
    SocketStream(int fd, std::uint32_t timeout_ms) : fd_(fd) {
        struct timeval timeout = {static_cast<time_t>(timeout_ms / 1000),
                                  static_cast<suseconds_t>((timeout_ms % 1000) * 1000)};
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
    ~SocketStream() override { if (fd_ >= 0) ::close(fd_); }
    IoResult receive(std::uint8_t *buffer, std::size_t capacity) override {
        const int n = ::recv(fd_, buffer, capacity, 0);
        if (n > 0) {
            ESP_LOGI(TAG, "TCP RX %d bytes: %.*s", n, n, reinterpret_cast<char *>(buffer));
            return {IoState::Data, static_cast<std::size_t>(n)};
        }
        if (n == 0) return {IoState::Disconnected, 0};
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
            return {IoState::Timeout, 0};
        ESP_LOGW(TAG, "Socket receive error %d", errno);
        return {IoState::Error, 0};
    }
    IoResult send(const std::uint8_t *data, std::size_t length) override {
        const int n = ::send(fd_, data, length, 0);
        if (n > 0) {
            ESP_LOGI(TAG, "TCP TX %d bytes: %.*s", n, n, reinterpret_cast<const char *>(data));
            return {IoState::Data, static_cast<std::size_t>(n)};
        }
        if (n == 0) return {IoState::Disconnected, 0};
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)
            return {IoState::Timeout, 0};
        ESP_LOGW(TAG, "Socket send error %d", errno);
        return {IoState::Error, 0};
    }
private:
    int fd_;
};

SyncService::SyncService(WifiDriver &wifi, EffectsEngine &effects, SyncLimits limits)
    : wifi_(wifi), effects_(effects), limits_(limits), store_(limits) {}
SyncService::~SyncService() { if (task_) vTaskDelete(task_); }

esp_err_t SyncService::restoreSavedSequence()
{
    std::string name;
    LightSequence sequence;
    esp_err_t result = nvm_.loadSequence(name, sequence, limits_);
    if (result != ESP_OK) return result;
    if (store_.replace(std::move(name), std::move(sequence)) != SyncStatus::Ok)
        return ESP_ERR_INVALID_RESPONSE;
    result = effects_.activate(*store_.active());
    if (result != ESP_OK) return result;
    effects_.notify({LightSyncEvent::SequenceActivated, 0, 0});
    ESP_LOGI(TAG, "Validated saved sequence restored from NVS and activated");
    return ESP_OK;
}

esp_err_t SyncService::startServer(const WifiSoftApConfig &config)
{
    if (task_) return ESP_ERR_INVALID_STATE;
    esp_err_t result = wifi_.startSoftAP(config);
    if (result != ESP_OK) return result;
    server_ = true;
    if (xTaskCreate(taskEntry, "luxflux_sync", 8192, this, 3, &task_) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t SyncService::startClient(const WifiStationConfig &config,
                                   const char *sequence_name, const char *host_override)
{
    if (task_ || !sequence_name || !*sequence_name) return ESP_ERR_INVALID_ARG;
    sequence_name_ = sequence_name;
    host_override_ = host_override ? host_override : "";
    esp_err_t result = wifi_.startStation(config);
    if (result != ESP_OK) return result;
    server_ = false;
    if (xTaskCreate(taskEntry, "luxflux_sync", 8192, this, 3, &task_) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void SyncService::taskEntry(void *argument)
{ static_cast<SyncService *>(argument)->taskLoop(); }

void SyncService::logFrame(const LightFrame &frame)
{
    unsigned total = 0;
    for (const auto &group : frame.groups) {
        ESP_LOGI(TAG, "RGB group: count=%u color=(%u,%u,%u)", group.pixel_count,
                 group.color.red, group.color.green, group.color.blue);
        total += group.pixel_count;
    }
    ESP_LOGI(TAG, "Validated frame: %u LEDs, duration=%lu ms", total,
             static_cast<unsigned long>(frame.duration_ms));
}

void SyncService::taskLoop()
{
    // SyncProtocol reports milestones here. The playback task receives the
    // committed sequence and events; it never reads the TCP socket itself.
    auto callback = [this](SyncProgress progress) {
        switch (progress.event) {
        case LightSyncEvent::LoadStarted: ESP_LOGI(TAG, "SYNC handshake started"); break;
        case LightSyncEvent::TransferStarted: ESP_LOGI(TAG, "SYNC handshake complete; transfer started"); break;
        case LightSyncEvent::FrameReceived: ESP_LOGI(TAG, "Frame %lu validated; sending ACK",
                                                     static_cast<unsigned long>(progress.completed)); break;
        case LightSyncEvent::TransferCompleted: ESP_LOGI(TAG, "EOF handled; transfer complete"); break;
        case LightSyncEvent::TransferFailed: ESP_LOGW(TAG, "Transfer rejected; previous sequence preserved"); break;
        case LightSyncEvent::SequenceActivated:
            if (store_.active()) {
                const esp_err_t saved = nvm_.saveSequence(store_.activeName(),
                                                          *store_.active(), limits_);
                if (saved == ESP_OK) ESP_LOGI(TAG, "Validated sequence saved to NVS");
                else ESP_LOGW(TAG, "Could not save sequence to NVS: %s", esp_err_to_name(saved));
                const esp_err_t activated = effects_.activate(*store_.active());
                if (activated != ESP_OK) {
                    ESP_LOGE(TAG, "Cannot activate sequence: %s", esp_err_to_name(activated));
                    return;
                }
                ESP_LOGI(TAG, "Validated sequence committed; effects task activating it");
            }
            break;
        default: break;
        }
        effects_.notify(progress);
    };
    SyncProtocol protocol(limits_, callback, [this](const LightFrame &frame) { logFrame(frame); });
    if (server_) {
        // ESP32 SoftAP-server role; independent of the Python server on a PC.
        SocketServer listener;
        for (;;) {
            if (!listener.listening() && listener.listen(kPort) != ESP_OK) {
                ESP_LOGE(TAG, "Cannot bind 0.0.0.0:%u; retrying", kPort);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            ESP_LOGI(TAG, "Listening on 0.0.0.0:%u", kPort);
            int client_fd = -1;
            if (listener.accept(client_fd, 1000) == ESP_OK) {
                SocketStream stream(client_fd, CONFIG_LUXFLUX_SYNC_SOCKET_TIMEOUT_MS);
                const SyncStatus status = protocol.serve(stream, store_);
                ESP_LOGI(TAG, "Server transfer ended: %s", syncStatusName(status));
                if (status != SyncStatus::Ok)
                    effects_.notify({LightSyncEvent::TransferFailed, 0, 0});
            }
        }
    }
    // ESP32 Station-client role: wait for Wi-Fi, connect, receive one sequence,
    // then retry later if the one-shot Python server has closed its socket.
    for (;;) {
        if (wifi_.waitForStation(10000) != ESP_OK) {
            ESP_LOGW(TAG, "Waiting for Station IP");
            continue;
        }
        char gateway[16] = {};
        if (wifi_.gatewayAddress(gateway, sizeof(gateway)) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        // Empty override uses the DHCP gateway; the lab setup uses the PC IP.
        const char *host = host_override_.empty() ? gateway : host_override_.c_str();
        SocketClient socket;
        ESP_LOGI(TAG, "Connecting to RGB server %s:%u", host, kPort);
        if (socket.connect(host, kPort, CONFIG_LUXFLUX_SYNC_SOCKET_TIMEOUT_MS) == ESP_OK) {
            SocketStream stream(socket.release(), CONFIG_LUXFLUX_SYNC_SOCKET_TIMEOUT_MS);
            const SyncStatus status = protocol.receive(stream, sequence_name_, store_);
            ESP_LOGI(TAG, "Client transfer ended: %s", syncStatusName(status));
        } else {
            effects_.notify({LightSyncEvent::TransferFailed, 0, 0});
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
}
