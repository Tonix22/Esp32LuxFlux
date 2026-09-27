#pragma once
#include "led_driver.h"

namespace luxflux {
// Blocking, bounded diagnostic sequence. Ends with all pixels off.
esp_err_t runLedDiagnostics(LedDriver &driver);
}
