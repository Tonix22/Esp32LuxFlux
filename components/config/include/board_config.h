#pragma once

#include "sdkconfig.h"

namespace luxflux::board {
inline constexpr int kLedDataGpio = CONFIG_LUXFLUX_LED_GPIO;
inline constexpr int kI2cSdaGpio = CONFIG_LUXFLUX_I2C_SDA_GPIO;
inline constexpr int kI2cSclGpio = CONFIG_LUXFLUX_I2C_SCL_GPIO;
inline constexpr int kImuInterruptGpio = CONFIG_LUXFLUX_IMU_INT_GPIO;
inline constexpr int kMicrophoneGpio = CONFIG_LUXFLUX_MIC_GPIO;
inline constexpr int kBatteryAdcGpio = CONFIG_LUXFLUX_BATTERY_ADC_GPIO;
inline constexpr int kStatusLedGpio = CONFIG_LUXFLUX_STATUS_LED_GPIO;
}
