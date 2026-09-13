#include <Arduino.h>

#include "adapters/lora_radio.h"
#include "app_config.h"
#include "send_mqtt.h"

#if !GATEWAY_V1_ENABLED
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#endif

namespace
{
#if GATEWAY_V1_ENABLED
/**
 * Report radio events during the T07 binary-adapter slice.
 *
 * T08 will replace the V1 FrameReceived diagnostic with the coordinator and
 * queue flow. Keeping this handler byte-oriented ensures the enabled V1 build
 * cannot call the previous text parser or MQTT publisher accidentally.
 */
void handleV1RadioEvent(const gateway::adapter::LoRaRadioEvent &event)
{
    switch (event.type)
    {
    case gateway::adapter::LoRaRadioEventType::FrameReceived:
        Serial.print("Data: Received binary LoRa frame, bytes: ");
        Serial.print(event.frameLength);
        Serial.print(" | RSSI: ");
        Serial.print(event.rssi);
        Serial.print(" dBm | SNR: ");
        Serial.println(static_cast<float>(event.snrX4) / 4.0F);
        break;

    case gateway::adapter::LoRaRadioEventType::FrameDropped:
        Serial.print("Warning: Dropped oversized or incomplete LoRa frame, reported bytes: ");
        Serial.println(event.frameLength);
        break;

    case gateway::adapter::LoRaRadioEventType::TransmitSucceeded:
        Serial.println("Status: LoRa transmission completed");
        break;

    case gateway::adapter::LoRaRadioEventType::TransmitFailed:
        Serial.println("Error: LoRa transmission timed out; receive mode restored");
        break;

    default:
        break;
    }
}
#else
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

/** Parse and publish one legacy frame outside the hardware adapter. */
void handleLegacyRadioEvent(const gateway::adapter::LoRaRadioEvent &event)
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
#endif

/** Poll the adapter once so loop remains responsive to Wi-Fi and MQTT work. */
void serviceRadio()
{
    gateway::adapter::LoRaRadioEvent event{};
    if (!gateway::adapter::serviceLoRaRadio(&event))
    {
        return;
    }

#if GATEWAY_V1_ENABLED
    handleV1RadioEvent(event);
#else
    handleLegacyRadioEvent(event);
#endif
}
} // namespace

void setup()
{
    // Start diagnostics before initializing network and radio services.
    Serial.begin(115200);
    delay(1000);

    // MQTT connection maintenance remains independent from the radio service.
    // The V1 path will begin producing queued telemetry in T08.
    initMqtt();

    Serial.print("Status: Starting LoRa at ");
    Serial.print(LORA_FREQUENCY / 1000000L);
    Serial.println(" MHz...");

    const gateway::adapter::LoRaRadioInitResult radioResult =
        gateway::adapter::initializeLoRaRadio();
    if (radioResult != gateway::adapter::LoRaRadioInitResult::Ok)
    {
        Serial.print("Error: LoRa initialization failed: ");
        Serial.println(gateway::adapter::loRaRadioInitResultName(radioResult));
        return;
    }

    Serial.println("Status: LoRa is ready in receive mode");
#if GATEWAY_V1_ENABLED
    Serial.println("Mode: Gateway protocol V1 binary adapter");
#else
    Serial.println("Mode: Legacy text telemetry compatibility");
#endif
}

void loop()
{
    // Both services perform bounded work and return without waiting for network
    // or radio activity, keeping the loop ready for the T08 coordinator tick.
    handleMqtt();
    serviceRadio();
}
