#include "app_config.h"

#if !GATEWAY_V1_ENABLED
#include "legacy_mode.h"
#include <Arduino.h>
#include "adapters/mqtt_transport.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
namespace gateway
{
namespace app
{
namespace legacy_mode
{
namespace
{
/**
 * Convert the old text telemetry into a bounded uppercase representation.
 * Spaces and quotes are removed to preserve the accepted legacy formats while
 * keeping all temporary storage on the stack. Binary frames containing a null
 * byte are rejected instead of being interpreted as a truncated C string.
 */
bool normalizeLegacyPayload(const gateway::adapter::LoRaRadioEvent &event,
                            char *output,
                            size_t outputCapacity)
{
    if (output == nullptr || outputCapacity == 0U ||
        event.frameLength >= outputCapacity)
    {
        return false;
    }

    size_t outputLength = 0U;
    for (size_t index = 0U; index < event.frameLength; ++index)
    {
        const unsigned char value = event.frame[index];
        if (value == 0U)
        {
            return false;
        }

        if (value == static_cast<unsigned char>('"') || isspace(value))
        {
            continue;
        }

        output[outputLength] = static_cast<char>(toupper(value));
        ++outputLength;
    }

    output[outputLength] = '\0';
    return outputLength > 0U;
}

/** Find a legacy field marker and parse the finite number that follows it. */
bool readLegacyNumber(const char *payload, const char *key, float &value)
{
    const char *keyPosition = strstr(payload, key);
    if (keyPosition == nullptr)
    {
        return false;
    }

    const char *numberStart = keyPosition + strlen(key);
    char *numberEnd = nullptr;
    value = strtof(numberStart, &numberEnd);
    return numberEnd != numberStart && isfinite(value);
}

/** Preserve the formats accepted before V1 while the build flag remains off. */
bool parseLegacyTelemetry(const char *payload,
                          float &temperature,
                          float &humidity)
{
    const bool hasTemperature =
        readLegacyNumber(payload, "TEMPERATURE:", temperature) ||
        readLegacyNumber(payload, "TEMPERATURE=", temperature) ||
        readLegacyNumber(payload, "T:", temperature) ||
        readLegacyNumber(payload, "T=", temperature);

    const bool hasHumidity =
        readLegacyNumber(payload, "HUMIDITY:", humidity) ||
        readLegacyNumber(payload, "HUMIDITY=", humidity) ||
        readLegacyNumber(payload, "H:", humidity) ||
        readLegacyNumber(payload, "H=", humidity);

    return hasTemperature && hasHumidity;
}

} // namespace

/** Parse and publish one legacy frame outside the hardware adapter. */
void handleRadio(const gateway::adapter::LoRaRadioEvent &event)
{
    if (event.type == gateway::adapter::LoRaRadioEventType::FrameDropped)
    {
        Serial.print("Warning: Dropped oversized or incomplete LoRa frame, reported bytes: ");
        Serial.println(event.frameLength);
        return;
    }

    if (event.type != gateway::adapter::LoRaRadioEventType::FrameReceived)
    {
        return;
    }

    char payload[gateway::adapter::LORA_RADIO_FRAME_CAPACITY + 1U] = {};
    if (!normalizeLegacyPayload(event, payload, sizeof(payload)))
    {
        Serial.println("Error: Invalid legacy LoRa payload");
        return;
    }

    Serial.print("Data: Received LoRa packet: ");
    Serial.print(payload);
    Serial.print(" | RSSI: ");
    Serial.print(event.rssi);
    Serial.print(" dBm | SNR: ");
    Serial.println(static_cast<float>(event.snrX4) / 4.0F);

    float temperature = 0.0F;
    float humidity = 0.0F;
    if (!parseLegacyTelemetry(payload, temperature, humidity))
    {
        Serial.println("Error: Invalid telemetry format. Example: T:25.5,H:60.2");
        return;
    }

    sendTelemetry(temperature, humidity);
}
} // namespace legacy_mode
} // namespace app
} // namespace gateway

#endif
