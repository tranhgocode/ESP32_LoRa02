#include "app_config.h"

#if GATEWAY_V1_ENABLED && !GATEWAY_UPLINK_ENABLED
#include "poll_mode.h"
#include <Arduino.h>
#include "adapters/mqtt_transport.h"
#include "gateway_packet.h"
#include "poll_coordinator.h"
#include "sensor_registry.h"
#include "telemetry_buffer.h"
#include "telemetry_delivery.h"
namespace gateway
{
namespace app
{
namespace poll_mode
{
namespace
{
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
gateway::TelemetryDeliveryService telemetryDeliveryService{};
PendingRadioTransmission pendingRadioTransmission =
    PendingRadioTransmission::None;

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

/** Copy one queued V1 JSON document into the asynchronous MQTT outbox. */
bool publishV1Telemetry(const char *payload,
                        size_t payloadLength,
                        void *)
{
    return sendTelemetryPayload(payload, payloadLength);
}

} // namespace

/**
 * Map one adapter event to the radio-independent coordinator vocabulary.
 * Dropped frames remain diagnostics only, while RX metadata travels with a
 * complete frame so an accepted sample retains RSSI and quarter-dB SNR.
 */
void handleRadio(const gateway::adapter::LoRaRadioEvent &radioEvent,
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
bool initialize(uint32_t nowMs)
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
    telemetryDeliveryService = gateway::TelemetryDeliveryService{};
    pendingRadioTransmission = PendingRadioTransmission::None;

    Serial.print("Status: V1 sensor registry ready, configured nodes: ");
    Serial.println(sensorRegistry.count);
    return true;
}

/** Main supplies ticks only after the registry and radio are ready. */
void serviceTick(uint32_t nowMs)
{
    gateway::CoordinatorEvent event{};
    event.type = gateway::CoordinatorEventType::Tick;
    event.nowMs = nowMs;
    processCoordinatorEvent(event);
}

/** Hand off at most one FIFO sample so MQTT work cannot starve the radio. */
void serviceTelemetry()
{
    // ESP-IDF 4.4.7 protects enqueue/outbox calls with MQTT_API_LOCK, which can
    // wait on network work. Only enter MQTT while no POLL/ACK transaction or
    // radio transmission is active; serviceRadio() has already run this loop.
    if (pendingRadioTransmission != PendingRadioTransmission::None ||
        pollCoordinator.phase != gateway::CoordinatorPhase::Idle)
    {
        return;
    }

    const gateway::TelemetryDeliveryResult result =
        gateway::serviceTelemetryDelivery(
            &telemetryBuffer,
            publishV1Telemetry,
            nullptr,
            &telemetryDeliveryService,
            millis());

    if (result == gateway::TelemetryDeliveryResult::EncodingFailed)
    {
        Serial.println("Error: Failed to serialize queued V1 telemetry");
    }
    else if (result == gateway::TelemetryDeliveryResult::InvalidArgument)
    {
        Serial.println("Error: Invalid V1 telemetry delivery state");
    }
}
} // namespace poll_mode
} // namespace app
} // namespace gateway

#endif
