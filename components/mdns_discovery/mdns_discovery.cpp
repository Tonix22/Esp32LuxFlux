#include "mdns_discovery.hpp"
#include "wifi_driver.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mdns.h"
#include "sdkconfig.h"
#include <cstdio>
#include <cstring>

namespace luxflux {
namespace {
constexpr const char *TAG = "luxflux_mdns";
constexpr const char *kService = "_luxflux";
constexpr const char *kProtocol = "_tcp";

const char *txtValue(const mdns_result_t *result, const char *key)
{
    for (size_t i = 0; i < result->txt_count; ++i)
        if (result->txt[i].key && std::strcmp(result->txt[i].key, key) == 0)
            return result->txt[i].value;
    return nullptr;
}
}

MdnsDiscovery::MdnsDiscovery() { mutex_ = xSemaphoreCreateMutex(); }

MdnsDiscovery::~MdnsDiscovery()
{
    if (task_) vTaskDelete(task_);
    if (mdns_ready_) mdns_free();
    if (mutex_) vSemaphoreDelete(mutex_);
}

esp_err_t MdnsDiscovery::start(WifiDriver &wifi, bool softap_server, bool upload_server)
{
    if (!mutex_) return ESP_ERR_NO_MEM;
    if (task_) return ESP_ERR_INVALID_STATE;
    uint8_t mac[6] = {};
    esp_err_t result = esp_efuse_mac_get_default(mac);
    if (result != ESP_OK) return result;
    char id[13] = {};
    std::snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    identity_.device_id = id;
    identity_.logical_name.clear();
    identity_.state = discovery::EnumerationState::INITIALIZING;
    xSemaphoreGive(mutex_);
    wifi_ = &wifi;
    softap_server_ = softap_server;
    upload_server_ = upload_server;
    ESP_LOGI(TAG, "Factory device ID: %s", id);
    if (xTaskCreate(taskEntry, "luxflux_mdns", 6144, this, 3, &task_) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

DiscoveryIdentity MdnsDiscovery::identity() const
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    DiscoveryIdentity result = identity_;
    const std::uint32_t generation = assigned_generation_;
    xSemaphoreGive(mutex_);
    if (result.state == discovery::EnumerationState::ASSIGNED && !softap_server_ &&
        (!wifi_->isConnected() || wifi_->connectionGeneration() != generation)) {
        result.state = discovery::EnumerationState::DISCOVERING;
        result.logical_name.clear();
    }
    return result;
}

bool MdnsDiscovery::assigned() const
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool ready = identity_.state == discovery::EnumerationState::ASSIGNED &&
        (softap_server_ || (wifi_ && wifi_->isConnected() &&
                            wifi_->connectionGeneration() == assigned_generation_));
    xSemaphoreGive(mutex_);
    return ready;
}

void MdnsDiscovery::setState(discovery::EnumerationState state, const char *name)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    identity_.state = state;
    if (name) identity_.logical_name = name;
    xSemaphoreGive(mutex_);
}

bool MdnsDiscovery::publish(const char *name, const char *state)
{
    const DiscoveryIdentity current = identity();
    mdns_txt_item_t txt[] = {
        {"device_id", current.device_id.c_str()},
        {"logical_name", name},
        {"state", state},
        {"role", tcpRole()}
    };
    const esp_err_t result = mdns_service_txt_set(kService, kProtocol, txt, 4);
    if (result != ESP_OK) ESP_LOGE(TAG, "Cannot publish identity: %s", esp_err_to_name(result));
    return result == ESP_OK;
}

std::vector<discovery::Peer> MdnsDiscovery::queryPeers(bool *query_succeeded)
{
    std::vector<discovery::Peer> peers;
    mdns_result_t *results = nullptr;
    const esp_err_t result = mdns_query_ptr(kService, kProtocol,
                         CONFIG_LUXFLUX_MDNS_QUERY_TIMEOUT_MS, 64, &results);
    if (query_succeeded) *query_succeeded = result == ESP_OK;
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Discovery query failed: %s", esp_err_to_name(result));
    } else {
        const std::string own_id = identity().device_id;
        for (const mdns_result_t *item = results; item; item = item->next) {
            const char *id = txtValue(item, "device_id");
            const char *name = txtValue(item, "logical_name");
            const char *state = txtValue(item, "state");
            if (!id || !name || !state || !discovery::validDeviceId(id) ||
                own_id == id || discovery::scientistIndex(name) < 0 ||
                (std::strcmp(state, "active") != 0 && std::strcmp(state, "claiming") != 0))
                continue;
            discovery::mergePeers(peers, {{id, name, std::strcmp(state, "active") == 0}});
        }
    }
    mdns_query_results_free(results);
    return peers;
}

std::vector<discovery::Peer> MdnsDiscovery::discoverThreeTimes(bool &any_query_succeeded)
{
    std::array<std::vector<discovery::Peer>, discovery::kDiscoveryAttempts> attempts;
    any_query_succeeded = false;
    ESP_LOGI(TAG, "Starting device discovery");
    for (unsigned attempt = 0; attempt < discovery::kDiscoveryAttempts; ++attempt) {
        ESP_LOGI(TAG, "Discovery attempt %u/%u", attempt + 1, discovery::kDiscoveryAttempts);
        bool succeeded = false;
        attempts[attempt] = queryPeers(&succeeded);
        any_query_succeeded |= succeeded;
        if (attempt + 1 < discovery::kDiscoveryAttempts)
            vTaskDelay(pdMS_TO_TICKS(CONFIG_LUXFLUX_MDNS_RETRY_INTERVAL_MS));
    }
    auto merged = discovery::mergeDiscoveryAttempts(attempts);
    for (const auto &peer : merged)
        ESP_LOGI(TAG, "Discovered: %s (%s, %s)", peer.logical_name.c_str(),
                 peer.device_id.c_str(), peer.active ? "active" : "claiming");
    return merged;
}

void MdnsDiscovery::pauseRandom(unsigned minimum_ms, unsigned maximum_ms)
{
    const unsigned delay = minimum_ms + esp_random() % (maximum_ms - minimum_ms + 1);
    vTaskDelay(pdMS_TO_TICKS(delay));
}

void MdnsDiscovery::taskEntry(void *argument)
{ static_cast<MdnsDiscovery *>(argument)->taskLoop(); }

void MdnsDiscovery::taskLoop()
{
    for (;;) {
        if (!softap_server_ && !wifi_->isConnected()) {
            const auto current = identity();
            if (mdns_ready_ && (current.state != discovery::EnumerationState::DISCOVERING ||
                                !current.logical_name.empty())) {
                setState(discovery::EnumerationState::DISCOVERING, "");
                publish("", "offline");
                ESP_LOGW(TAG, "Wi-Fi disconnected; identity withdrawn");
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (!mdns_ready_) {
            const esp_err_t init = mdns_init();
            if (init != ESP_OK) {
                ESP_LOGE(TAG, "mDNS initialization failed: %s", esp_err_to_name(init));
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            const std::string id = identity().device_id;
            const std::string instance = "luxflux-" + id;
            esp_err_t result = mdns_hostname_set(instance.c_str());
            if (result == ESP_OK) {
                mdns_txt_item_t txt[] = {
                    {"device_id", id.c_str()}, {"logical_name", ""},
                    {"state", "discovering"}, {"role", tcpRole()}
                };
                result = mdns_service_add(instance.c_str(), kService, kProtocol,
                                          (softap_server_ || upload_server_) ? 3333 : 0, txt, 4);
            }
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "mDNS service registration failed: %s", esp_err_to_name(result));
                mdns_free();
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            mdns_ready_ = true;
            ESP_LOGI(TAG, "Advertising %s._luxflux._tcp.local", instance.c_str());
        }
        setState(discovery::EnumerationState::DISCOVERING, "");
        publish("", "discovering");
        ESP_LOGI(TAG, "Enumerating after network connection");
        const std::uint32_t network_generation = wifi_->connectionGeneration();
        pauseRandom(100, 500);

        while (softap_server_ || (wifi_->isConnected() &&
                                  wifi_->connectionGeneration() == network_generation)) {
            bool discovery_succeeded = false;
            const auto peers = discoverThreeTimes(discovery_succeeded);
            if (!softap_server_ && (!wifi_->isConnected() ||
                wifi_->connectionGeneration() != network_generation)) break;
            if (!discovery_succeeded) {
                ESP_LOGW(TAG, "All discovery queries failed; delaying enumeration");
                vTaskDelay(pdMS_TO_TICKS(CONFIG_LUXFLUX_MDNS_RETRY_INTERVAL_MS));
                continue;
            }
            setState(discovery::EnumerationState::SELECTING_NAME);
            const char *name = discovery::firstAvailable(peers, identity().device_id);
            if (!name) {
                setState(discovery::EnumerationState::NO_AVAILABLE_NAME, "");
                publish("", "exhausted");
                ESP_LOGE(TAG, "No logical identifier available; all 26 names occupied");
                for (int elapsed = 0; elapsed < CONFIG_LUXFLUX_MDNS_VERIFY_INTERVAL_MS;
                     elapsed += 250) {
                    vTaskDelay(pdMS_TO_TICKS(250));
                    if (!softap_server_ && (!wifi_->isConnected() ||
                        wifi_->connectionGeneration() != network_generation)) break;
                }
                continue;
            }
            ESP_LOGI(TAG, "First available name: %s", name);
            setState(discovery::EnumerationState::CLAIMING_NAME, name);
            if (!publish(name, "claiming")) {
                setState(discovery::EnumerationState::CONFLICT, "");
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            ESP_LOGI(TAG, "Claiming: %s", name);
            pauseRandom(250, 750);
            setState(discovery::EnumerationState::VERIFYING);
            ESP_LOGI(TAG, "Verifying assignment");
            bool verification_succeeded = false;
            const auto verified = discoverThreeTimes(verification_succeeded);
            if (!softap_server_ && (!wifi_->isConnected() ||
                wifi_->connectionGeneration() != network_generation)) break;
            if (!verification_succeeded) {
                ESP_LOGW(TAG, "All collision queries failed; withdrawing unverified claim");
                setState(discovery::EnumerationState::CONFLICT, "");
                publish("", "conflict");
                vTaskDelay(pdMS_TO_TICKS(CONFIG_LUXFLUX_MDNS_RETRY_INTERVAL_MS));
                continue;
            }
            const std::string id = identity().device_id;
            if (discovery::mustYield(id, false, name, verified)) {
                ESP_LOGW(TAG, "Collision on %s; withdrawing provisional claim", name);
                setState(discovery::EnumerationState::CONFLICT, "");
                publish("", "conflict");
                pauseRandom(300, 1000);
                continue;
            }
            if (!publish(name, "active")) {
                setState(discovery::EnumerationState::CONFLICT, "");
                continue;
            }
            xSemaphoreTake(mutex_, portMAX_DELAY);
            assigned_generation_ = network_generation;
            identity_.state = discovery::EnumerationState::ASSIGNED;
            xSemaphoreGive(mutex_);
            ESP_LOGI(TAG, "Assignment confirmed: %s (%s)", name, id.c_str());

            while (softap_server_ || (wifi_->isConnected() &&
                                      wifi_->connectionGeneration() == network_generation)) {
                for (int elapsed = 0; elapsed < CONFIG_LUXFLUX_MDNS_VERIFY_INTERVAL_MS;
                     elapsed += 250) {
                    vTaskDelay(pdMS_TO_TICKS(250));
                    if (!softap_server_ && (!wifi_->isConnected() ||
                        wifi_->connectionGeneration() != network_generation)) break;
                }
                if (!softap_server_ && (!wifi_->isConnected() ||
                    wifi_->connectionGeneration() != network_generation)) break;
                const auto current = queryPeers();
                if (discovery::mustYield(id, true, name, current)) {
                    ESP_LOGW(TAG, "Confirmed collision on %s; lower factory MAC wins", name);
                    setState(discovery::EnumerationState::CONFLICT, "");
                    publish("", "conflict");
                    pauseRandom(300, 1000);
                    break;
                }
            }
            if (identity().state == discovery::EnumerationState::CONFLICT) continue;
            break;
        }
    }
}
} // namespace luxflux
