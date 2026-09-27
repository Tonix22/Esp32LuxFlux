#include "wifi_driver.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <array>
#include <cstring>

namespace luxflux {
static constexpr const char *TAG = "wifi_driver";

#if CONFIG_LUXFLUX_WIFI_DIAGNOSTIC_SCAN
static const char *authModeName(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3-PSK";
    default: return "other";
    }
}
#endif

static const char *disconnectReasonName(std::uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "authentication expired";
    case WIFI_REASON_AUTH_FAIL: return "authentication failed";
    case WIFI_REASON_ASSOC_FAIL: return "association failed";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "WPA handshake timeout";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "WPA handshake failed";
    case WIFI_REASON_CONNECTION_FAIL: return "connection failed";
    default: return "other";
    }
}

WifiDriver::~WifiDriver() { stop(); }

esp_err_t WifiDriver::initialize()
{
    if (initialized_) return ESP_OK;
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) return result;
    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = esp_event_loop_create_default();
    if (result == ESP_OK) owns_event_loop_ = true;
    else if (result != ESP_ERR_INVALID_STATE) return result;
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi_config);
    if (result != ESP_OK) return result;
    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  eventHandler, this, &wifi_handler_);
    if (result != ESP_OK) { esp_wifi_deinit(); return result; }
    result = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  eventHandler, this, &ip_handler_);
    if (result != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_);
        wifi_handler_ = nullptr;
        esp_wifi_deinit();
        return result;
    }
    initialized_ = true;
    return ESP_OK;
}

void WifiDriver::eventHandler(void *argument, esp_event_base_t base, std::int32_t id, void *event_data)
{
    auto &self = *static_cast<WifiDriver *>(argument);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!self.manual_connect_.load()) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self.connected_.store(false);
        self.connection_generation_.fetch_add(1);
        const auto *event = static_cast<const wifi_event_sta_disconnected_t *>(event_data);
        const std::uint8_t reason = event ? event->reason : 0;
        ESP_LOGW(TAG, "Station disconnected: %s (reason=%u); reconnecting",
                 disconnectReasonName(reason), static_cast<unsigned>(reason));
        if (self.running_) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<const ip_event_got_ip_t *>(event_data);
        if (!self.connected_.load() || (event && event->ip_changed))
            self.connection_generation_.fetch_add(1);
        self.connected_.store(true);
        if (event) ESP_LOGI(TAG, "Station connected; IP=" IPSTR,
                            IP2STR(&event->ip_info.ip));
    }
}

esp_err_t WifiDriver::start(WifiMode mode, const WifiStationConfig *station,
                            const WifiSoftApConfig *softap)
{
    if (!initialized_ || running_) return ESP_ERR_INVALID_STATE;
    wifi_mode_t idf_mode = WIFI_MODE_NULL;
    if (station) {
        if (!station->ssid || !station->password || !station->ssid[0] ||
            std::strlen(station->ssid) > 32 || std::strlen(station->password) > 63)
            return ESP_ERR_INVALID_ARG;
        station_netif_ = esp_netif_create_default_wifi_sta();
        if (!station_netif_) return ESP_ERR_NO_MEM;
        idf_mode = WIFI_MODE_STA;
    }
    if (softap) {
        const std::size_t ssid_length = softap->ssid ? std::strlen(softap->ssid) : 0;
        const std::size_t password_length = softap->password ? std::strlen(softap->password) : 0;
        if (!softap->password || ssid_length == 0 || ssid_length > 32 ||
            (password_length != 0 && (password_length < 8 || password_length > 63)) ||
            softap->channel < 1 || softap->channel > 13 ||
            softap->maximum_connections < 1 || softap->maximum_connections > 4) {
            if (station_netif_) { esp_netif_destroy_default_wifi(station_netif_); station_netif_ = nullptr; }
            return ESP_ERR_INVALID_ARG;
        }
        softap_netif_ = esp_netif_create_default_wifi_ap();
        if (!softap_netif_) {
            if (station_netif_) { esp_netif_destroy_default_wifi(station_netif_); station_netif_ = nullptr; }
            return ESP_ERR_NO_MEM;
        }
        idf_mode = station ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    }
    // ESP32WOL relies on ESP-IDF's default persistent Wi-Fi config storage.
    esp_err_t result = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (result == ESP_OK) result = esp_wifi_set_mode(idf_mode);
    if (result == ESP_OK && station) {
        wifi_config_t config = {};
        std::memcpy(config.sta.ssid, station->ssid, std::strlen(station->ssid));
        std::memcpy(config.sta.password, station->password, std::strlen(station->password));
        if (station->password[0]) config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_LOGI(TAG, "Station target SSID '%s'; password length=%u (value hidden)",
                 station->ssid, static_cast<unsigned>(std::strlen(station->password)));
        result = esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    if (result == ESP_OK && softap) {
        wifi_config_t config = {};
        std::memcpy(config.ap.ssid, softap->ssid, std::strlen(softap->ssid));
        std::memcpy(config.ap.password, softap->password, std::strlen(softap->password));
        config.ap.ssid_len = std::strlen(softap->ssid);
        config.ap.channel = softap->channel;
        config.ap.max_connection = softap->maximum_connections;
        config.ap.authmode = softap->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        result = esp_wifi_set_config(WIFI_IF_AP, &config);
    }
#if CONFIG_LUXFLUX_WIFI_DIAGNOSTIC_SCAN
    manual_connect_.store(station != nullptr);
#else
    manual_connect_.store(false);
#endif
    if (result == ESP_OK) result = esp_wifi_start();
    if (result == ESP_OK && station) {
        const esp_err_t ps_result = esp_wifi_set_ps(WIFI_PS_NONE);
        ESP_LOGI(TAG, "Station power save disabled: %s", esp_err_to_name(ps_result));
        if (ps_result != ESP_OK) {
            esp_wifi_stop();
            result = ps_result;
        }
    }
    if (result != ESP_OK) {
        manual_connect_.store(false);
        if (station_netif_) { esp_netif_destroy_default_wifi(station_netif_); station_netif_ = nullptr; }
        if (softap_netif_) { esp_netif_destroy_default_wifi(softap_netif_); softap_netif_ = nullptr; }
        return result;
    }
    running_ = true;
    mode_ = mode;
    ESP_LOGI(TAG, "Wi-Fi mode %d started", static_cast<int>(mode));
#if CONFIG_LUXFLUX_WIFI_DIAGNOSTIC_SCAN
    if (station) {
        std::array<std::uint8_t, 33> target_ssid = {};
        std::memcpy(target_ssid.data(), station->ssid, std::strlen(station->ssid));
        wifi_scan_config_t scan_config = {};
        scan_config.ssid = target_ssid.data();
        ESP_LOGI(TAG, "Scanning for target SSID before connecting");
        const esp_err_t scan_result = esp_wifi_scan_start(&scan_config, true);
        if (scan_result == ESP_OK) {
            std::uint16_t found = 0;
            const esp_err_t count_result = esp_wifi_scan_get_ap_num(&found);
            if (count_result == ESP_OK && found > 0) {
                std::array<wifi_ap_record_t, 16> records = {};
                std::uint16_t count = found > records.size() ? records.size() : found;
                const esp_err_t record_result = esp_wifi_scan_get_ap_records(&count, records.data());
                if (record_result == ESP_OK) {
                    ESP_LOGI(TAG, "Target SSID scan found %u BSSID(s)", static_cast<unsigned>(found));
                    for (std::uint16_t i = 0; i < count; ++i) {
                        const auto &ap = records[i];
                        ESP_LOGI(TAG, "Candidate AP %u: channel=%u RSSI=%d dBm auth=%s(%d) pairwise=%d group=%d",
                                 static_cast<unsigned>(i + 1), static_cast<unsigned>(ap.primary),
                                 static_cast<int>(ap.rssi), authModeName(ap.authmode),
                                 static_cast<int>(ap.authmode), static_cast<int>(ap.pairwise_cipher),
                                 static_cast<int>(ap.group_cipher));
                    }
                } else {
                    ESP_LOGW(TAG, "Could not read AP scan records: %s", esp_err_to_name(record_result));
                }
            } else {
                ESP_LOGW(TAG, "Target SSID not found in diagnostic scan");
                esp_wifi_clear_ap_list();
            }
        } else {
            ESP_LOGW(TAG, "Diagnostic scan failed: %s", esp_err_to_name(scan_result));
            esp_wifi_clear_ap_list();
        }
        const esp_err_t connect_result = esp_wifi_connect();
        if (connect_result != ESP_OK)
            ESP_LOGE(TAG, "Initial Station connection request failed: %s",
                     esp_err_to_name(connect_result));
    }
#endif
    return ESP_OK;
}

esp_err_t WifiDriver::startStation(const WifiStationConfig &config)
{ return start(WifiMode::Station, &config, nullptr); }
esp_err_t WifiDriver::startSoftAP(const WifiSoftApConfig &config)
{ return start(WifiMode::SoftAP, nullptr, &config); }
esp_err_t WifiDriver::startStationAndSoftAP(const WifiStationConfig &station_config,
                                            const WifiSoftApConfig &softap_config)
{ return start(WifiMode::StationAndSoftAP, &station_config, &softap_config); }

esp_err_t WifiDriver::waitForStation(std::uint32_t timeout_ms)
{
    if (!running_ || !station_netif_) return ESP_ERR_INVALID_STATE;
    for (std::uint32_t elapsed = 0; elapsed < timeout_ms; elapsed += 100) {
        if (connected_.load()) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return connected_.load() ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t WifiDriver::gatewayAddress(char *buffer, std::size_t length) const
{
    if (!buffer || length < 16 || !station_netif_ || !connected_.load()) return ESP_ERR_INVALID_STATE;
    esp_netif_ip_info_t info = {};
    const esp_err_t result = esp_netif_get_ip_info(station_netif_, &info);
    if (result != ESP_OK) return result;
    return esp_ip4addr_ntoa(&info.gw, buffer, length) ? ESP_OK : ESP_FAIL;
}

esp_err_t WifiDriver::stop()
{
    running_ = false;
    connected_.store(false);
    manual_connect_.store(false);
    if (initialized_) {
        esp_wifi_stop();
        if (wifi_handler_) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_);
        if (ip_handler_) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_);
        wifi_handler_ = nullptr;
        ip_handler_ = nullptr;
        esp_wifi_deinit();
    }
    if (station_netif_) { esp_netif_destroy_default_wifi(station_netif_); station_netif_ = nullptr; }
    if (softap_netif_) { esp_netif_destroy_default_wifi(softap_netif_); softap_netif_ = nullptr; }
    if (owns_event_loop_) { esp_event_loop_delete_default(); owns_event_loop_ = false; }
    initialized_ = false;
    mode_ = WifiMode::Disabled;
    return ESP_OK;
}
}
