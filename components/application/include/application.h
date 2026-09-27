#pragma once
#include "esp_err.h"
#include "led_driver.h"
#include "effects_engine.h"
#include "sync_service.h"
#include "wifi_driver.h"
#include "mdns_discovery.hpp"

namespace luxflux {
class Application {
public:
    Application();
    esp_err_t initialize();
    void run();
private:
    WifiDriver wifi_;
    MdnsDiscovery discovery_;
    LedDriver leds_;
    EffectsEngine effects_;
    SyncService sync_;
};
}
