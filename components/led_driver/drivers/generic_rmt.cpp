#include "generic_rmt.h"
#include "sdkconfig.h"
namespace luxflux {
LedTiming genericRmtTiming()
{
    return {"Generic RMT", CONFIG_LUXFLUX_LED_GENERIC_T0H_NS,
            CONFIG_LUXFLUX_LED_GENERIC_T0L_NS, CONFIG_LUXFLUX_LED_GENERIC_T1H_NS,
            CONFIG_LUXFLUX_LED_GENERIC_T1L_NS, CONFIG_LUXFLUX_LED_GENERIC_RESET_US};
}
}
