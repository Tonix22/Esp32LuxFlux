#pragma once
#include "effects_engine.h"
#include "sync_protocol.h"
#include "wifi_driver.h"
#include "nvm_store.h"
#include "mdns_discovery.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace luxflux {
class SyncService {
public:
    SyncService(WifiDriver &wifi, MdnsDiscovery &discovery, EffectsEngine &effects, SyncLimits limits);
    ~SyncService();
    esp_err_t startServer(const WifiSoftApConfig &config);
    esp_err_t startUploadServer(const WifiStationConfig &config, const char *sequence_name);
    esp_err_t startClient(const WifiStationConfig &config,
                          const char *sequence_name, const char *host_override);
    SequenceStore &store() { return store_; }
    esp_err_t restoreSavedSequence();
private:
    static void taskEntry(void *argument);
    void taskLoop();
    void logFrame(const LightFrame &frame);
    WifiDriver &wifi_;
    MdnsDiscovery &discovery_;
    EffectsEngine &effects_;
    SyncLimits limits_;
    SequenceStore store_;
    nvm::NvmStore nvm_;
    TaskHandle_t task_ = nullptr;
    bool server_ = false;
    bool upload_server_ = false;
    std::string sequence_name_;
    std::string host_override_;
};
}
