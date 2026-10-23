#include "app_config.h"

#include "gateway_app.h"
#include "adapters/uart_reporter.h"
#include "gateway_packet.h"
#include "sensor_registry.h"
#include "uplink_coordinator.h"
#include "gateway_delivery.h"
#include "adapters/mqtt_transport.h"
namespace gateway
{
namespace app
{
namespace gateway_app
{
namespace
{
gateway::SensorRegistry sensorRegistry{};
gateway::UplinkCoordinator uplinkCoordinator{};
bool uplinkAckPending = false;
TelemetryBuffer telemetryBuffer{};
GatewayDelivery cloudDelivery{};

static_assert(gateway::MAX_FRAME_LENGTH <=
                  gateway::adapter::LORA_RADIO_FRAME_CAPACITY,
              "Radio adapter cannot carry a maximum-size V1 frame");

void processUplinkEvent(const gateway::UplinkEvent &event)
{
    gateway::UplinkAction action = gateway::handleUplinkEvent(
        &uplinkCoordinator, &sensorRegistry, &event);
    if (action.type == gateway::UplinkActionType::RecordReport)
    {
        gateway::UplinkEvent confirmation{};
        confirmation.type = (action.hasSample ? pushTelemetrySample(&telemetryBuffer, &action.sample) : gateway::adapter::recordUplinkReport(action))
                                ? gateway::UplinkEventType::ReportAccepted
                                : gateway::UplinkEventType::ReportRejected;
        action = gateway::handleUplinkEvent(
            &uplinkCoordinator, &sensorRegistry, &confirmation);
    }
    if (action.type != gateway::UplinkActionType::SendAck)
    {
        return;
    }
    uint8_t frame[gateway::MAX_FRAME_LENGTH] = {};
    size_t length = 0U;
    if (gateway::encodePacket(&action.packet, frame, sizeof(frame), &length) &&
        gateway::adapter::startLoRaTransmit(frame, length) ==
            gateway::adapter::LoRaTransmitStartResult::Started)
    {
        uplinkAckPending = true;
        return;
    }
    gateway::UplinkEvent failure{};
    failure.type = gateway::UplinkEventType::AckTxFailed;
    gateway::handleUplinkEvent(&uplinkCoordinator, &sensorRegistry, &failure);
}
} // namespace

void handleRadio(const gateway::adapter::LoRaRadioEvent &radioEvent,
                        uint32_t nowMs)
{
    gateway::UplinkEvent event{};
    if (radioEvent.type == gateway::adapter::LoRaRadioEventType::FrameReceived)
    {
        event.frame = radioEvent.frame;
        event.frameLength = radioEvent.frameLength;
        event.nowMs = nowMs;
        event.rssi = radioEvent.rssi;
        event.snrX4 = radioEvent.snrX4;
    }
    else if (uplinkAckPending &&
             (radioEvent.type == gateway::adapter::LoRaRadioEventType::TransmitSucceeded ||
              radioEvent.type == gateway::adapter::LoRaRadioEventType::TransmitFailed))
    {
        uplinkAckPending = false;
        event.type = radioEvent.type == gateway::adapter::LoRaRadioEventType::TransmitSucceeded
                         ? gateway::UplinkEventType::AckTxSucceeded
                         : gateway::UplinkEventType::AckTxFailed;
    }
    else
    {
        return;
    }
    processUplinkEvent(event);
}

bool initialize()
{
    uplinkAckPending = false;
    resetTelemetryBuffer(&telemetryBuffer);
    cloudDelivery = GatewayDelivery{};
    gateway::initializeUplinkCoordinator(&uplinkCoordinator);
    return gateway::initializeSensorRegistry(
               &sensorRegistry, gateway_config::SENSOR_NODES,
               gateway_config::SENSOR_NODE_COUNT) ==
           gateway::RegistryInitResult::Ok;
}
namespace
{
bool publish(const char *topic, const char *payload, size_t length, void *)
{
    return enqueueMqttPayload(topic, payload, length, cloudDelivery.generation);
}
}
void serviceTelemetry(uint32_t nowMs)
{
    if (uplinkAckPending || uplinkCoordinator.phase != UplinkPhase::Idle) return;
    serviceGatewayDelivery(&cloudDelivery, &sensorRegistry, &telemetryBuffer,
        mqttSession(), publish, nullptr, nowMs);
}
} // namespace gateway_app
} // namespace app
} // namespace gateway

