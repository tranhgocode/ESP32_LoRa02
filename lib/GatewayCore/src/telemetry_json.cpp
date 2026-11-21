#include "telemetry_json.h"
#include "battery_level.h"

#include <stdio.h>
#include <string.h>

namespace gateway
{
namespace
{
/** Keep partial JSON private until the entire payload fits the output buffer. */
template <size_t Capacity>
class JsonBuffer
{
public:
    template <typename... Args>
    bool append(const char *format, Args... args)
    {
        const size_t available = Capacity - length;
        const int written = snprintf(bytes + length, available, format, args...);
        if (written < 0 || static_cast<size_t>(written) >= available)
        {
            return false;
        }
        length += static_cast<size_t>(written);
        return true;
    }

    bool copyTo(char *output, size_t capacity, size_t *outputLength) const
    {
        if (length >= capacity)
        {
            return false;
        }
        memcpy(output, bytes, length + 1U);
        *outputLength = length;
        return true;
    }

private:
    char bytes[Capacity] = {};
    size_t length = 0U;
};

using TelemetryJson = JsonBuffer<TELEMETRY_JSON_CAPACITY>;

// Widen the signed inputs before negation to support their full ranges.
unsigned long magnitude(int32_t value)
{
    return static_cast<unsigned long>(value < 0 ? -value : value);
}

bool appendBaseTelemetry(TelemetryJson &json, const TelemetrySample &sample)
{
    const unsigned long temperature = magnitude(sample.temperatureX10);
    const unsigned long humidity = sample.humidityX10;
    const unsigned long snr = magnitude(sample.snrX4);

    // Keep fixed-point values exact without floating-point formatting.
    return json.append(
        "{\"node_id\":%u,\"sequence\":%u,"
        "\"temperature\":%s%lu.%lu,\"humidity\":%lu.%lu,"
        "\"rssi\":%d,\"snr\":%s%lu.%02lu",
        static_cast<unsigned int>(sample.nodeId),
        static_cast<unsigned int>(sample.sequence),
        sample.temperatureX10 < 0 ? "-" : "",
        temperature / 10UL,
        temperature % 10UL,
        humidity / 10UL,
        humidity % 10UL,
        static_cast<int>(sample.rssi),
        sample.snrX4 < 0 ? "-" : "",
        snr / 4UL,
        (snr % 4UL) * 25UL);
}

bool appendBatteryLevel(TelemetryJson &json, uint16_t millivolts, const BatteryCurve &curve)
{
    uint8_t percent = 0U;
    return !calculateBatteryLevel(millivolts, curve, percent) ||
           json.append(",\"batteryLevel\":%u", static_cast<unsigned int>(percent));
}

bool appendForecast(TelemetryJson &json, const uint16_t (&forecast)[PM25_FORECAST_COUNT])
{
    // Publish a forecast only when all four values came from the same report.
    for (uint16_t value : forecast)
    {
        if (value == SENSOR_VALUE_UNAVAILABLE)
        {
            return true;
        }
    }
    for (size_t step = 0U; step < PM25_FORECAST_COUNT; ++step)
    {
        if (!json.append(",\"pm25_forecast_step_%u\":%u",
                         static_cast<unsigned int>(step + 1U),
                         static_cast<unsigned int>(forecast[step])))
        {
            return false;
        }
    }
    return true;
}

bool appendTelemetry(TelemetryJson &json, const TelemetrySample &sample, const BatteryCurve &curve)
{
    const OptionalSensorData &data = sample.optional;
    return appendBaseTelemetry(json, sample) &&
           appendBatteryLevel(json, data.batteryMv, curve) &&
           (data.pm25UgM3 == SENSOR_VALUE_UNAVAILABLE ||
            json.append(",\"pm25\":%u", static_cast<unsigned int>(data.pm25UgM3))) &&
           appendForecast(json, data.pm25Forecast) &&
           json.append("}");
}
} // namespace

bool serializeTelemetryJson(const TelemetrySample *sample,
                            char *output,
                            size_t outputCapacity,
                            size_t *outputLength)
{
    if (sample == nullptr || output == nullptr || outputLength == nullptr)
    {
        return false;
    }

    TelemetryJson json;
    return appendTelemetry(json, *sample, BatteryCurve{}) &&
           json.copyTo(output, outputCapacity, outputLength);
}

bool serializeGatewayTelemetryJson(const TelemetrySample *sample, const SensorRegistry *registry,
                                   char *output, size_t capacity, size_t *length)
{
    if (sample == nullptr || output == nullptr || length == nullptr)
    {
        return false;
    }
    const SensorNodeConfig *config = findNodeConfig(registry, sample->nodeId);
    if (config == nullptr || !config->enabled || !isValidDeviceName(config->deviceName))
    {
        return false;
    }

    TelemetryJson json;
    return json.append("{\"%s\":[", config->deviceName) &&
           appendTelemetry(json, *sample, config->batteryCurve) &&
           json.append("]}") &&
           json.copyTo(output, capacity, length);
}

bool serializeGatewayConnectJson(const SensorNodeConfig *config, char *output, size_t capacity, size_t *length)
{
    if (config == nullptr || output == nullptr || length == nullptr ||
        !config->enabled || !isValidDeviceName(config->deviceName))
    {
        return false;
    }

    JsonBuffer<DEVICE_NAME_CAPACITY + 16U> json;
    return json.append("{\"device\":\"%s\"}", config->deviceName) &&
           json.copyTo(output, capacity, length);
}
} // namespace gateway
