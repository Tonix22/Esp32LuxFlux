#include "led_protocol.h"
#include "sdkconfig.h"
// Kconfig selects one protocol at build time. Each backend supplies pulse
// timings; LedDriver uses the same RMT transmit path for every backend.
#if CONFIG_LUXFLUX_LED_WS2812B
#include "ws2812b.h"
#elif CONFIG_LUXFLUX_LED_SK6812
#include "sk6812.h"
#elif CONFIG_LUXFLUX_LED_WS2811_800
#include "ws2811_800.h"
#elif CONFIG_LUXFLUX_LED_WS2811_400
#include "ws2811_400.h"
#elif CONFIG_LUXFLUX_LED_UCS1903
#include "ucs1903.h"
#elif CONFIG_LUXFLUX_LED_SM16703
#include "sm16703.h"
#elif CONFIG_LUXFLUX_LED_GENERIC
#include "generic_rmt.h"
#else
#error "An LED backend must be selected"
#endif

namespace luxflux {
LedTiming selectedLedTiming()
{
#if CONFIG_LUXFLUX_LED_WS2812B
    return ws2812bTiming();
#elif CONFIG_LUXFLUX_LED_SK6812
    return sk6812Timing();
#elif CONFIG_LUXFLUX_LED_WS2811_800
    return ws2811_800Timing();
#elif CONFIG_LUXFLUX_LED_WS2811_400
    return ws2811_400Timing();
#elif CONFIG_LUXFLUX_LED_UCS1903
    return ucs1903Timing();
#elif CONFIG_LUXFLUX_LED_SM16703
    return sm16703Timing();
#elif CONFIG_LUXFLUX_LED_GENERIC
    return genericRmtTiming();
#endif
}
}
