#pragma once

#include "gateway_model.h"
#include "gateway_packet.h"
#include "telemetry_sample.h"

namespace gateway
{
enum class UplinkPhase : uint8_t
{
    Idle,
    AwaitingRecord,
    AckTxPending
};
enum class UplinkEventType : uint8_t
{
    FrameReceived,
    ReportAccepted,
    ReportRejected,
    AckTxSucceeded,
    AckTxFailed
};
enum class UplinkActionType : uint8_t
{
    None,
    RecordReport,
    SendAck
};

struct UplinkEvent
{
    UplinkEventType type = UplinkEventType::FrameReceived;
    const uint8_t *frame = nullptr;
    size_t frameLength = 0U;
    uint32_t nowMs = 0U;
    int16_t rssi = 0;
    int8_t snrX4 = 0;
};

/** RecordReport proposes validated data; it does not authorize ACK yet.
 * Main must record/log it and report acceptance or rejection synchronously.
 * ERROR has report metadata and an error code, but never a DATA sample.
 */
struct UplinkAction
{
    UplinkActionType type = UplinkActionType::None;
    PacketMessage packet{};
    bool hasSample = false;
    TelemetrySample sample{};
    PacketErrorCode errorCode = PacketErrorCode::DhtInitFailed;
    int16_t rssi = 0;
    int8_t snrX4 = 0;
    uint32_t receivedAtMs = 0U;
};

struct UplinkCoordinator
{
    UplinkPhase phase = UplinkPhase::Idle;
    PacketMessage pendingReport{};
};

void initializeUplinkCoordinator(UplinkCoordinator *coordinator);

/** Pure transition: no clock, registry mutation, radio, queue or network calls.
 * One report/ACK slot; incoming frames while occupied are ignored. There is no
 * sequence deduplication: reset, wrap and repeated ID/Seq remain valid reports.
 * Both ACK completion outcomes release the slot without replaying the sample.
 */
UplinkAction handleUplinkEvent(UplinkCoordinator *coordinator, const SensorRegistry *registry,
                               const UplinkEvent *event);
} // namespace gateway
