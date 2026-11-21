#include "gateway_app.h"

#include "app_config.h"
#include "adapters/mqtt_transport.h"
#include "adapters/uart_reporter.h"
#include "sensor_registry.h"
#include "uplink_coordinator.h"

namespace gateway
{
namespace app
{
namespace gateway_app
{
namespace
{
enum class AckState
{
    Idle,
    WaitingForReceiver,
    Transmitting
};

SensorRegistry sensorRegistry{};
UplinkCoordinator uplinkCoordinator{};
TelemetryBuffer telemetryBuffer{};
GatewayDelivery cloudDelivery{};
AckState ackState = AckState::Idle;
PacketMessage pendingAck{};
uint16_t pendingBatteryMv = SENSOR_VALUE_UNAVAILABLE;
uint32_t ackScheduledAtMs = 0U;

static_assert(MAX_FRAME_LENGTH <= adapter::LORA_RADIO_FRAME_CAPACITY,
              "Radio adapter cannot carry a maximum-size V1 frame");

void recordReceivedFrame(const adapter::LoRaRadioEvent &radioEvent, uint32_t nowMs)
{
    UplinkEvent event{};
    event.frame = radioEvent.frame;
    event.frameLength = radioEvent.frameLength;
    event.nowMs = nowMs;
    event.rssi = radioEvent.rssi;
    event.snrX4 = radioEvent.snrX4;

    const UplinkAction report = handleUplinkEvent(&uplinkCoordinator, &sensorRegistry, &event);
    if (report.type != UplinkActionType::RecordReport)
    {
        adapter::reportRejectedFrame(event.frameLength);
        return;
    }

    const bool recorded = report.hasSample
                              ? pushTelemetrySample(&telemetryBuffer, &report.sample)
                              : adapter::recordUplinkReport(report);
    event.type = recorded ? UplinkEventType::ReportAccepted : UplinkEventType::ReportRejected;
    const UplinkAction ack = handleUplinkEvent(&uplinkCoordinator, &sensorRegistry, &event);
    if (ack.type == UplinkActionType::SendAck)
    {
        pendingAck = ack.packet;
        pendingBatteryMv = report.sample.optional.batteryMv;
        ackScheduledAtMs = nowMs;
        ackState = AckState::WaitingForReceiver;
        if (report.hasSample)
        {
            // The sample is already queued. Logging only uses available UART space.
            adapter::recordUplinkReport(report);
        }
    }
}

void finishAck(bool sent)
{
    UplinkEvent event{};
    event.type = sent ? UplinkEventType::AckTxSucceeded : UplinkEventType::AckTxFailed;
    handleUplinkEvent(&uplinkCoordinator, &sensorRegistry, &event);
    ackState = AckState::Idle;
    // Report only after the radio has returned to reception.
    adapter::reportAckResult(pendingAck, sent, pendingBatteryMv,
                             findNodeConfig(&sensorRegistry, pendingAck.destination));
}

bool publish(const char *topic, const char *payload, size_t length, void *)
{
    if (!enqueueMqttPayload(topic, payload, length, cloudDelivery.generation))
    {
        return false;
    }
    adapter::reportMqttPayload(topic, payload, length);
    return true;
}
} // namespace

bool initialize()
{
    ackState = AckState::Idle;
    resetTelemetryBuffer(&telemetryBuffer);
    cloudDelivery = GatewayDelivery{};
    initializeUplinkCoordinator(&uplinkCoordinator);
    return initializeSensorRegistry(&sensorRegistry, gateway_config::SENSOR_NODES,
                                    gateway_config::SENSOR_NODE_COUNT) == RegistryInitResult::Ok;
}

void handleRadio(const adapter::LoRaRadioEvent &event, uint32_t nowMs)
{
    switch (event.type)
    {
    case adapter::LoRaRadioEventType::FrameReceived:
        if (ackState == AckState::Idle)
        {
            recordReceivedFrame(event, nowMs);
        }
        break;
    case adapter::LoRaRadioEventType::FrameDropped:
        adapter::reportRejectedFrame(event.frameLength);
        break;
    case adapter::LoRaRadioEventType::TransmitSucceeded:
    case adapter::LoRaRadioEventType::TransmitFailed:
        if (ackState == AckState::Transmitting)
        {
            finishAck(event.type == adapter::LoRaRadioEventType::TransmitSucceeded);
        }
        break;
    default:
        break;
    }
}

void servicePendingAck(uint32_t nowMs)
{
    if (ackState != AckState::WaitingForReceiver ||
        nowMs - ackScheduledAtMs < LORA_ACK_TURNAROUND_MS)
    {
        return;
    }

    uint8_t frame[MAX_FRAME_LENGTH] = {};
    size_t length = 0U;
    if (encodePacket(&pendingAck, frame, sizeof(frame), &length) &&
        adapter::startLoRaTransmit(frame, length) == adapter::LoRaTransmitStartResult::Started)
    {
        ackState = AckState::Transmitting;
    }
    else
    {
        finishAck(false);
    }
}

void serviceTelemetry(uint32_t nowMs)
{
    if (ackState == AckState::Idle)
    {
        serviceGatewayDelivery(&cloudDelivery, &sensorRegistry, &telemetryBuffer,
                               mqttSession(), publish, nullptr, nowMs);
    }
}
} // namespace gateway_app
} // namespace app
} // namespace gateway

