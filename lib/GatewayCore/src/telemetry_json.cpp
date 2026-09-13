#include "telemetry_json.h"

#include <stdio.h>
#include <string.h>

namespace gateway
{
namespace
{
/**
 * Split a signed fixed-point value without overflowing on its minimum value.
 * raw is widened to int32_t before negation, so all int16_t and int8_t values
 * used by TelemetrySample have a representable positive magnitude.
 */
uint32_t signedMagnitude(int32_t raw)
{
    return raw < 0 ? static_cast<uint32_t>(-raw)
                   : static_cast<uint32_t>(raw);
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

    const int32_t temperatureRaw = sample->temperatureX10;
    const uint32_t temperatureMagnitude = signedMagnitude(temperatureRaw);
    const char *temperatureSign = temperatureRaw < 0 ? "-" : "";

    const uint32_t humidityMagnitude = sample->humidityX10;

    const int32_t snrRaw = sample->snrX4;
    const uint32_t snrMagnitude = signedMagnitude(snrRaw);
    const char *snrSign = snrRaw < 0 ? "-" : "";

    // Build into private storage so snprintf truncation can never expose a
    // partial JSON document to the MQTT adapter. Casts match each variadic
    // format argument explicitly on both native and ESP32 builds.
    char candidate[TELEMETRY_JSON_CAPACITY] = {};
    const int written = snprintf(
        candidate,
        sizeof(candidate),
        "{\"node_id\":%u,\"sequence\":%u,"
        "\"temperature\":%s%lu.%lu,\"humidity\":%lu.%lu,"
        "\"rssi\":%d,\"snr\":%s%lu.%02lu}",
        static_cast<unsigned int>(sample->nodeId),
        static_cast<unsigned int>(sample->sequence),
        temperatureSign,
        static_cast<unsigned long>(temperatureMagnitude / 10U),
        static_cast<unsigned long>(temperatureMagnitude % 10U),
        static_cast<unsigned long>(humidityMagnitude / 10U),
        static_cast<unsigned long>(humidityMagnitude % 10U),
        static_cast<int>(sample->rssi),
        snrSign,
        static_cast<unsigned long>(snrMagnitude / 4U),
        static_cast<unsigned long>((snrMagnitude % 4U) * 25U));

    // snprintf returns the number of bytes it wanted to write, excluding the
    // terminator. A negative value is an encoding error; a value equal to or
    // larger than capacity means the internal buffer was truncated.
    if (written < 0 ||
        static_cast<size_t>(written) >= sizeof(candidate))
    {
        return false;
    }

    const size_t payloadLength = static_cast<size_t>(written);
    const size_t requiredCapacity = payloadLength + 1U;
    if (outputCapacity < requiredCapacity)
    {
        return false;
    }

    // Include the null byte in the copy, then publish the byte length only
    // after every possible failure has been ruled out.
    memcpy(output, candidate, requiredCapacity);
    *outputLength = payloadLength;
    return true;
}
} // namespace gateway
