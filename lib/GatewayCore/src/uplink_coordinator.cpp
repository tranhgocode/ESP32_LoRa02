#include "uplink_coordinator.h"
#include "sensor_registry.h"

namespace gateway
{
namespace
{
constexpr int16_t MIN_TEMPERATURE_X10 = -400;
constexpr int16_t MAX_TEMPERATURE_X10 = 800;
constexpr uint16_t MAX_HUMIDITY_X10 = 1000U;

bool buildTelemetrySample(const PacketMessage &packet, const UplinkEvent &event,
                          TelemetrySample &sample)
{
    SensorData data{};
    if (!decodeDataPayload(&packet, &data) ||
        data.temperatureX10 < MIN_TEMPERATURE_X10 ||
        data.temperatureX10 > MAX_TEMPERATURE_X10 ||
        data.humidityX10 > MAX_HUMIDITY_X10)
    {
        return false;
    }

    sample.nodeId = packet.source;
    sample.sequence = packet.sequence;
    sample.temperatureX10 = data.temperatureX10;
    sample.humidityX10 = data.humidityX10;
    sample.rssi = event.rssi;
    sample.snrX4 = event.snrX4;
    sample.receivedAtMs = event.nowMs;
    // Copy all optional fields so a short report clears values from older reports.
    sample.optional = data.optional;
    return true;
}

UplinkAction receiveReport(UplinkCoordinator &coordinator, const SensorRegistry &registry,
                           const UplinkEvent &event)
{
    PacketMessage packet{};
    if (event.type != UplinkEventType::FrameReceived ||
        !decodePacket(event.frame, event.frameLength, &packet) ||
        (packet.type != PacketType::Data && packet.type != PacketType::Error))
    {
        return UplinkAction{};
    }
    const SensorNodeConfig *config = findNodeConfig(&registry, packet.source);
    if (config == nullptr || !config->enabled)
    {
        return UplinkAction{};
    }

    UplinkAction action{};
    if (packet.type == PacketType::Data)
    {
        if (!buildTelemetrySample(packet, event, action.sample))
        {
            return UplinkAction{};
        }
        action.hasSample = true;
    }
    else if (!decodeErrorPayload(&packet, &action.errorCode))
    {
        return UplinkAction{};
    }
    action.type = UplinkActionType::RecordReport;
    action.packet = packet;
    action.rssi = event.rssi;
    action.snrX4 = event.snrX4;
    action.receivedAtMs = event.nowMs;
    coordinator.pendingReport = packet;
    coordinator.phase = UplinkPhase::AwaitingRecord;
    return action;
}

UplinkAction handleRecordResult(UplinkCoordinator &coordinator, UplinkEventType eventType)
{
    if (eventType == UplinkEventType::ReportRejected)
    {
        initializeUplinkCoordinator(&coordinator);
    }
    if (eventType != UplinkEventType::ReportAccepted)
    {
        return UplinkAction{};
    }

    // Echo the identifiers only after the application records the report.
    const PacketMessage &report = coordinator.pendingReport;
    UplinkAction action{};
    action.type = UplinkActionType::SendAck;
    action.packet.type = PacketType::Ack;
    action.packet.source = GATEWAY_ADDRESS;
    action.packet.destination = report.source;
    action.packet.transactionId = report.transactionId;
    action.packet.sequence = report.sequence;
    coordinator.phase = UplinkPhase::AckTxPending;
    return action;
}
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

    switch (coordinator->phase)
    {
    case UplinkPhase::Idle:
        return receiveReport(*coordinator, *registry, *event);
    case UplinkPhase::AwaitingRecord:
        return handleRecordResult(*coordinator, event->type);
    case UplinkPhase::AckTxPending:
        if (event->type == UplinkEventType::AckTxSucceeded ||
            event->type == UplinkEventType::AckTxFailed)
        {
            initializeUplinkCoordinator(coordinator);
        }
        break;
    default:
        initializeUplinkCoordinator(coordinator);
        break;
    }
    return UplinkAction{};
}
} // namespace gateway
