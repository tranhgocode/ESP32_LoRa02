#include "uplink_coordinator.h"
#include "sensor_registry.h"

namespace gateway
{
namespace
{
constexpr int16_t MIN_TEMPERATURE_X10 = -400;
constexpr int16_t MAX_TEMPERATURE_X10 = 800;
constexpr uint16_t MAX_HUMIDITY_X10 = 1000U;
} // namespace

void initializeUplinkCoordinator(UplinkCoordinator *coordinator)
{
    if (coordinator != nullptr)
    {
        *coordinator = UplinkCoordinator{};
    }
}

UplinkAction handleUplinkEvent(UplinkCoordinator *coordinator, const SensorRegistry *registry,
                               const UplinkEvent *event)
{
    if (coordinator == nullptr || registry == nullptr || event == nullptr)
    {
        return UplinkAction{};
    }

    if (coordinator->phase == UplinkPhase::AwaitingRecord)
    {
        if (event->type == UplinkEventType::ReportRejected)
        {
            initializeUplinkCoordinator(coordinator);
        }
        else if (event->type == UplinkEventType::ReportAccepted)
        {
            UplinkAction action{};
            action.type = UplinkActionType::SendAck;
            action.packet.type = PacketType::Ack;
            action.packet.source = GATEWAY_ADDRESS;
            action.packet.destination = coordinator->pendingReport.source;
            action.packet.transactionId = coordinator->pendingReport.transactionId;
            action.packet.sequence = coordinator->pendingReport.sequence;
            coordinator->phase = UplinkPhase::AckTxPending;
            return action;
        }
        return UplinkAction{};
    }
    if (coordinator->phase == UplinkPhase::AckTxPending)
    {
        if (event->type == UplinkEventType::AckTxSucceeded ||
            event->type == UplinkEventType::AckTxFailed)
        {
            initializeUplinkCoordinator(coordinator);
        }
        return UplinkAction{};
    }
    if (coordinator->phase != UplinkPhase::Idle)
    {
        initializeUplinkCoordinator(coordinator);
        return UplinkAction{};
    }
    if (event->type != UplinkEventType::FrameReceived)
    {
        return UplinkAction{};
    }

    PacketMessage packet{};
    if (!decodePacket(event->frame, event->frameLength, &packet) ||
        packet.destination != GATEWAY_ADDRESS ||
        (packet.type != PacketType::Data && packet.type != PacketType::Error))
    {
        return UplinkAction{};
    }
    const SensorNodeConfig *config = findNodeConfig(registry, packet.source);
    if (config == nullptr || !config->enabled)
    {
        return UplinkAction{};
    }

    UplinkAction action{};
    if (packet.type == PacketType::Data)
    {
        int16_t temperature = 0;
        uint16_t humidity = 0;
        if (!decodeDataPayload(&packet, &temperature, &humidity) ||
            temperature < MIN_TEMPERATURE_X10 || temperature > MAX_TEMPERATURE_X10 ||
            humidity > MAX_HUMIDITY_X10)
        {
            return UplinkAction{};
        }
        action.hasSample = true;
        action.sample.nodeId = packet.source;
        action.sample.sequence = packet.sequence;
        action.sample.temperatureX10 = temperature;
        action.sample.humidityX10 = humidity;
        action.sample.rssi = event->rssi;
        action.sample.snrX4 = event->snrX4;
        action.sample.receivedAtMs = event->nowMs;
    }
    else if (!decodeErrorPayload(&packet, &action.errorCode))
    {
        return UplinkAction{};
    }
    action.type = UplinkActionType::RecordReport;
    action.packet = packet;
    action.rssi = event->rssi;
    action.snrX4 = event->snrX4;
    action.receivedAtMs = event->nowMs;
    coordinator->pendingReport = packet;
    coordinator->phase = UplinkPhase::AwaitingRecord;
    return action;
}
} // namespace gateway
