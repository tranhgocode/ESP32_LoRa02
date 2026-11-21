#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gateway
{
constexpr uint16_t SENSOR_VALUE_UNAVAILABLE = UINT16_MAX;
constexpr size_t PM25_FORECAST_COUNT = 4U;

/** Optional fields retain the wire units and the missing-value marker. */
struct OptionalSensorData
{
    uint16_t batteryMv = SENSOR_VALUE_UNAVAILABLE;
    uint16_t pm25UgM3 = SENSOR_VALUE_UNAVAILABLE;
    uint16_t pm25Forecast[PM25_FORECAST_COUNT] = {
        SENSOR_VALUE_UNAVAILABLE, SENSOR_VALUE_UNAVAILABLE,
        SENSOR_VALUE_UNAVAILABLE, SENSOR_VALUE_UNAVAILABLE};
};

struct SensorData
{
    int16_t temperatureX10 = 0;
    uint16_t humidityX10 = 0U;
    OptionalSensorData optional{};
};
} // namespace gateway
