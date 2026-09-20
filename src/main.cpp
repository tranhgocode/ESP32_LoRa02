#include <Arduino.h>

#include "adapters/lora_radio.h"
#include "app_config.h"
#include "send_mqtt.h"

#if GATEWAY_V1_ENABLED
#include "gateway_packet.h"
#include "poll_coordinator.h"
#include "sensor_registry.h"
#include "telemetry_buffer.h"
#else
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#endif

namespace
{
#if GATEWAY_V1_ENABLED
/**
 * Identify which coordinator action owns the adapter's one asynchronous TX.
 * The application clears this value before reporting completion so a resulting
 * coordinator transition can start its next transmission immediately.
 */
enum class PendingRadioTransmission : uint8_t
{
    None = 0U,
    Poll,
    Ack
};

gateway::SensorRegistry sensorRegistry{};
gateway::PollCoordinator pollCoordinator{};
gateway::TelemetryBuffer telemetryBuffer{};
PendingRadioTransmission pendingRadioTransmission =
    PendingRadioTransmission::None;
bool gatewayV1Ready = false;

static_assert(gateway::MAX_FRAME_LENGTH <=
                  gateway::adapter::LORA_RADIO_FRAME_CAPACITY,
              "Radio adapter cannot carry a maximum-size V1 frame");

void processCoordinatorEvent(const gateway::CoordinatorEvent &event);

/** Add an accepted DATA sample to the bounded queue exactly once. */
void enqueueCoordinatorSample(const gateway::TelemetrySample &sample)
{
    const uint32_t droppedBefore = telemetryBuffer.telemetryDropped;
    if (!gateway::pushTelemetrySample(&telemetryBuffer, &sample))
    {
        Serial.println("Error: Failed to append V1 sample to RAM queue");
        return;
    }

    if (telemetryBuffer.telemetryDropped != droppedBefore)
    {
        Serial.println("Warning: RAM queue full, oldest V1 sample was dropped");
    }

    Serial.print("Data: V1 sample queued, node: ");
    Serial.print(sample.nodeId);
    Serial.print(" | sequence: ");
    Serial.print(sample.sequence);
    Serial.print(" | queue size: ");
    Serial.println(gateway::telemetryBufferSize(&telemetryBuffer));
}

/** Convert an action kind into the TX completion event expected by the core. */
gateway::CoordinatorEventType transmitResultEventType(
    PendingRadioTransmission transmission,
    bool succeeded)
{
    if (transmission == PendingRadioTransmission::Poll)
    {
        return succeeded
                   ? gateway::CoordinatorEventType::PollTxSucceeded
                   : gateway::CoordinatorEventType::PollTxFailed;
    }

    return succeeded
               ? gateway::CoordinatorEventType::AckTxSucceeded
               : gateway::CoordinatorEventType::AckTxFailed;
}

/** Tell the coordinator that a requested packet could not start transmission. */
void reportTransmitStartFailure(PendingRadioTransmission transmission,
                                uint32_t nowMs)
{
    gateway::CoordinatorEvent event{};
    event.type = transmitResultEventType(transmission, false);
    event.nowMs = nowMs;
    processCoordinatorEvent(event);
}

/**
 * Encode and start one POLL or ACK without blocking for radio completion.
 * Immediate codec/driver failures are converted back into coordinator events,
 * keeping transaction state consistent when no later adapter event will exist.
 */
void startCoordinatorTransmission(const gateway::CoordinatorAction &action,
                                  uint32_t nowMs)
{
    const PendingRadioTransmission requestedTransmission =
        action.type == gateway::CoordinatorActionType::SendPoll
            ? PendingRadioTransmission::Poll
            : PendingRadioTransmission::Ack;

    uint8_t frame[gateway::MAX_FRAME_LENGTH] = {};
    size_t frameLength = 0U;
    if (!gateway::encodePacket(
            &action.packet, frame, sizeof(frame), &frameLength))
    {
        Serial.println("Error: Coordinator produced an invalid V1 packet");
        reportTransmitStartFailure(requestedTransmission, nowMs);
        return;
    }

    const gateway::adapter::LoRaTransmitStartResult result =
        gateway::adapter::startLoRaTransmit(frame, frameLength);
    if (result != gateway::adapter::LoRaTransmitStartResult::Started)
    {
        Serial.print("Error: Could not start V1 transmission: ");
        Serial.println(gateway::adapter::loRaTransmitStartResultName(result));
        reportTransmitStartFailure(requestedTransmission, nowMs);
        return;
    }

    pendingRadioTransmission = requestedTransmission;
    Serial.print(requestedTransmission == PendingRadioTransmission::Poll
                     ? "Status: V1 POLL started, node: "
                     : "Status: V1 ACK started, node: ");
    Serial.print(action.packet.destination);
    Serial.print(" | transaction: ");
    Serial.print(action.packet.transactionId);
    Serial.print(" | sequence: ");
    Serial.println(action.packet.sequence);
}

/** Execute the bounded side effects returned by one pure state transition. */
void dispatchCoordinatorAction(const gateway::CoordinatorAction &action,
                               uint32_t nowMs)
{
    if (action.hasSample)
    {
        enqueueCoordinatorSample(action.sample);
    }

    if (action.type == gateway::CoordinatorActionType::SendPoll ||
        action.type == gateway::CoordinatorActionType::SendAck)
    {
        startCoordinatorTransmission(action, nowMs);
    }
}

/**
 * Pass exactly one event through GatewayCore and execute the returned action.
 * Capturing the active node before a timeout transition preserves useful logs
 * after the coordinator clears its transaction fields.
 */
void processCoordinatorEvent(const gateway::CoordinatorEvent &event)
{
    const gateway::CoordinatorPhase previousPhase = pollCoordinator.phase;
    const uint8_t previousNode = pollCoordinator.activeNodeAddress;
    const gateway::CoordinatorAction action = gateway::handleCoordinatorEvent(
        &pollCoordinator, &sensorRegistry, &event);

    if (event.type == gateway::CoordinatorEventType::Tick &&
        previousPhase == gateway::CoordinatorPhase::WaitingForResponse &&
        pollCoordinator.phase == gateway::CoordinatorPhase::Idle)
    {
        Serial.print("Warning: V1 response timeout, node: ");
        Serial.println(previousNode);
    }

    dispatchCoordinatorAction(action, event.nowMs);
}

/**
 * Map one adapter event to the radio-independent coordinator vocabulary.
 * Dropped frames remain diagnostics only, while RX metadata travels with a
 * complete frame so an accepted sample retains RSSI and quarter-dB SNR.
 */
void handleV1RadioEvent(const gateway::adapter::LoRaRadioEvent &radioEvent,
                        uint32_t nowMs)
{
    if (radioEvent.type == gateway::adapter::LoRaRadioEventType::FrameDropped)
    {
        Serial.print("Warning: Dropped oversized or incomplete LoRa frame, reported bytes: ");
        Serial.println(radioEvent.frameLength);
        return;
    }

    gateway::CoordinatorEvent event{};
    event.nowMs = nowMs;

    if (radioEvent.type == gateway::adapter::LoRaRadioEventType::FrameReceived)
    {
        event.type = gateway::CoordinatorEventType::FrameReceived;
        event.frame = radioEvent.frame;
        event.frameLength = radioEvent.frameLength;
        event.rssi = radioEvent.rssi;
        event.snrX4 = radioEvent.snrX4;
        processCoordinatorEvent(event);
        return;
    }

    const bool isTransmitResult =
        radioEvent.type ==
            gateway::adapter::LoRaRadioEventType::TransmitSucceeded ||
        radioEvent.type == gateway::adapter::LoRaRadioEventType::TransmitFailed;
    if (!isTransmitResult)
    {
        return;
    }

    if (pendingRadioTransmission == PendingRadioTransmission::None)
    {
        Serial.println("Warning: Ignored LoRa TX result without an owning V1 action");
        return;
    }

    const PendingRadioTransmission completedTransmission =
        pendingRadioTransmission;
    pendingRadioTransmission = PendingRadioTransmission::None;
    const bool succeeded =
        radioEvent.type ==
        gateway::adapter::LoRaRadioEventType::TransmitSucceeded;
    event.type = transmitResultEventType(completedTransmission, succeeded);

    if (!succeeded)
    {
        Serial.println("Error: LoRa transmission timed out, receive mode restored");
    }

    processCoordinatorEvent(event);
}

/** Validate static node configuration and reset all V1 runtime owners. */
bool initializeGatewayV1(uint32_t nowMs)
{
    const gateway::RegistryInitResult registryResult =
        gateway::initializeSensorRegistry(
            &sensorRegistry,
            gateway_config::SENSOR_NODES,
            gateway_config::SENSOR_NODE_COUNT,
            nowMs);
    if (registryResult != gateway::RegistryInitResult::Ok)
    {
        Serial.print("Error: V1 sensor registry initialization failed: ");
        Serial.println(gateway::registryInitResultName(registryResult));
        return false;
    }

    gateway::initializePollCoordinator(&pollCoordinator);
    gateway::resetTelemetryBuffer(&telemetryBuffer);
    pendingRadioTransmission = PendingRadioTransmission::None;

    Serial.print("Status: V1 sensor registry ready, configured nodes: ");
    Serial.println(sensorRegistry.count);
    return true;
}

/** Supply a clock tick only after both the registry and radio are ready. */
void serviceGatewayV1Tick()
{
    if (!gatewayV1Ready)
    {
        return;
    }

    gateway::CoordinatorEvent event{};
    event.type = gateway::CoordinatorEventType::Tick;
    event.nowMs = millis();
    processCoordinatorEvent(event);
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

/** Poll the adapter once and process at most one event without waiting. */
void serviceRadio()
{
    gateway::adapter::LoRaRadioEvent event{};
    if (!gateway::adapter::serviceLoRaRadio(&event))
    {
        return;
    }

#if GATEWAY_V1_ENABLED
    if (gatewayV1Ready)
    {
        handleV1RadioEvent(event, millis());
    }
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

    // MQTT startup requests Wi-Fi/client connection asynchronously. The V1
    // queue is intentionally not drained into MQTT until the later MQTT task.
    initMqtt();

#if GATEWAY_V1_ENABLED
    if (!initializeGatewayV1(millis()))
    {
        return;
    }
#endif

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
    gatewayV1Ready = true;
    Serial.println("Mode: Gateway protocol V1, one node to RAM queue");
#else
    Serial.println("Mode: Legacy text telemetry compatibility");
#endif
}

void loop()
{
    // Service radio first so TX completion and received DATA reach the
    // coordinator with minimum loop-induced delay.
    serviceRadio();
#if GATEWAY_V1_ENABLED
    serviceGatewayV1Tick();
#endif

    // Wi-Fi retry is time-gated and MQTT runs asynchronously. In V1 mode this
    // call never reads or removes samples from the RAM queue.
    handleMqtt();
}
