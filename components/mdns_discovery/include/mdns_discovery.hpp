#pragma once

#include "enumeration.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <cstdint>
#include <string>
#include <vector>

namespace luxflux {
class WifiDriver;

struct DiscoveryIdentity {
    std::string device_id;
    std::string logical_name;
    discovery::EnumerationState state = discovery::EnumerationState::INITIALIZING;
};

class MdnsDiscovery {
public:
    MdnsDiscovery();
    ~MdnsDiscovery();
    MdnsDiscovery(const MdnsDiscovery &) = delete;
    MdnsDiscovery &operator=(const MdnsDiscovery &) = delete;
    esp_err_t start(WifiDriver &wifi, bool softap_server);
    DiscoveryIdentity identity() const;
    bool assigned() const;
private:
    static void taskEntry(void *argument);
    void taskLoop();
    void setState(discovery::EnumerationState state, const char *name = nullptr);
    bool publish(const char *name, const char *state);
    std::vector<discovery::Peer> queryPeers(bool *query_succeeded = nullptr);
    std::vector<discovery::Peer> discoverThreeTimes(bool &any_query_succeeded);
    void pauseRandom(unsigned minimum_ms, unsigned maximum_ms);
    WifiDriver *wifi_ = nullptr;
    TaskHandle_t task_ = nullptr;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    DiscoveryIdentity identity_;
    bool softap_server_ = false;
    bool mdns_ready_ = false;
    std::uint32_t assigned_generation_ = 0;
};
} // namespace luxflux
