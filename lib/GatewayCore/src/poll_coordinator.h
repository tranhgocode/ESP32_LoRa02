#pragma once

#include "gateway_packet.h"
#include "sensor_registry.h"
#include "telemetry_sample.h"

#include <stddef.h>
#include <stdint.h>

namespace gateway
{
// A sensor response received at this elapsed time is already late. Keeping the
// protocol value in the core lets the adapter report timestamps without owning
// transaction policy.
constexpr uint32_t RESPONSE_TIMEOUT_MS = 1000U;

/**
 * Radio-independent stages of the single active LoRa transaction.
 *
 * A stage that ends in TxPending must receive an explicit success or failure
 * event from the adapter. This is what prevents a new POLL from being emitted
 * while the radio is still sending a previous POLL or ACK.
 */
enum class CoordinatorPhase : uint8_t
{
    Idle = 0U,
    PollTxPending,
    WaitingForResponse,
    AckTxPending
};

/** Events that main.cpp can create from its clock and LoRa adapter callbacks. */
enum class CoordinatorEventType : uint8_t
{
    Tick = 0U,
    PollTxSucceeded,
    PollTxFailed,
    FrameReceived,
    AckTxSucceeded,
    AckTxFailed
};

/** The only radio operations that the pure coordinator can request. */
enum class CoordinatorActionType : uint8_t
{
    None = 0U,
    SendPoll,
    SendAck
};

/**
 * One input delivered to the coordinator by the application loop.
 *
 * nowMs must come from the same wrapping millisecond clock for every event.
 * frame and frameLength are read only for FrameReceived and only during the
 * function call. RSSI is dBm and snrX4 stores quarter-dB units, matching the
 * fixed-point TelemetrySample representation without heap or float conversion
 * in the adapter boundary.
 */
struct CoordinatorEvent
{
    CoordinatorEventType type = CoordinatorEventType::Tick;
    uint32_t nowMs = 0U;
    const uint8_t *frame = nullptr;
    size_t frameLength = 0U;
    int16_t rssi = 0;
    int8_t snrX4 = 0;
};

/**
 * Output of one state transition.
 *
 * packet is populated when type requests SendPoll or SendAck. A newly accepted
 * in-range DATA response also sets hasSample and fills sample in the same
 * action, so main.cpp can enqueue it exactly once while dispatching the ACK.
 * ERROR, duplicate, and out-of-range DATA responses never set hasSample.
 */
struct CoordinatorAction
{
    CoordinatorActionType type = CoordinatorActionType::None;
    PacketMessage packet{};
    bool hasSample = false;
    TelemetrySample sample{};
};

/**
 * Persistent state for at most one active radio transaction.
 *
 * activeNodeAddress and transactionId identify every response that may mutate
 * registry state. acceptedResponse is retained only while an ACK is pending so
 * a byte-equivalent sensor retry can receive the same ACK without producing a
 * second sample or incrementing a counter twice. nextTransactionId advances
 * whenever a POLL action is emitted and naturally wraps from 255 to 0.
 * nextPollIndex starts the next due-node scan after the most recently polled
 * registry slot, providing a stable round-robin tie break.
 */
struct PollCoordinator
{
    CoordinatorPhase phase = CoordinatorPhase::Idle;
    uint8_t activeNodeAddress = GATEWAY_ADDRESS;
    uint8_t transactionId = 0U;
    uint8_t nextTransactionId = 0U;
    uint8_t nextPollIndex = 0U;
    uint32_t responseStartedAtMs = 0U;
    PacketMessage acceptedResponse{};
};

/**
 * Reset all transaction state and choose the ID used by the next POLL.
 * Passing 255 is useful for validating the required transaction-ID wrap.
 * A null pointer is accepted as a safe no-op.
 */
void initializePollCoordinator(PollCoordinator *coordinator,
                               uint8_t firstTransactionId = 0U);

/**
 * Apply one clock/radio event and return at most one requested side effect.
 *
 * The function decodes and validates raw frames before changing registry data,
 * uses unsigned elapsed-time arithmetic across millis() wrap, saturates every
 * counter it owns, and never calls a clock, radio, queue, or MQTT API directly.
 * A null argument returns an empty action without changing state.
 */
CoordinatorAction handleCoordinatorEvent(PollCoordinator *coordinator,
                                         SensorRegistry *registry,
                                         const CoordinatorEvent *event);
} // namespace gateway
