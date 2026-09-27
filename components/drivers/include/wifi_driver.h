#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

namespace luxflux {
enum class WifiMode { Disabled, Station, SoftAP, StationAndSoftAP };
struct WifiStationConfig { const char *ssid; const char *password; };
struct WifiSoftApConfig {
    const char *ssid;
    const char *password;
    std::uint8_t channel;
    std::uint8_t maximum_connections;
};

class WifiDriver {
public:
    ~WifiDriver();
    esp_err_t initialize();
    esp_err_t startStation(const WifiStationConfig &config);
    esp_err_t startSoftAP(const WifiSoftApConfig &config);
    esp_err_t startStationAndSoftAP(const WifiStationConfig &station_config,
                                   const WifiSoftApConfig &softap_config);
    esp_err_t stop();
    esp_err_t waitForStation(std::uint32_t timeout_ms);
    esp_err_t gatewayAddress(char *buffer, std::size_t length) const;
    WifiMode mode() const { return mode_; }
    bool isConnected() const { return connected_.load(); }
private:
    static void eventHandler(void *argument, esp_event_base_t base, std::int32_t id, void *data);
    esp_err_t start(WifiMode mode, const WifiStationConfig *station,
                    const WifiSoftApConfig *softap);
    bool initialized_ = false;
    bool running_ = false;
    bool owns_event_loop_ = false;
    std::atomic<bool> connected_{false};
    std::atomic<bool> manual_connect_{false};
    WifiMode mode_ = WifiMode::Disabled;
    esp_netif_t *station_netif_ = nullptr;
    esp_netif_t *softap_netif_ = nullptr;
    esp_event_handler_instance_t wifi_handler_ = nullptr;
    esp_event_handler_instance_t ip_handler_ = nullptr;
};
}
