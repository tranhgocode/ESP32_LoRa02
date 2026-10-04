#include "poll_coordinator.h"

#include <limits.h>

namespace gateway
{
namespace
{
constexpr int16_t MIN_TEMPERATURE_X10 = -400;
constexpr int16_t MAX_TEMPERATURE_X10 = 800;
constexpr uint16_t MAX_HUMIDITY_X10 = 1000U;
constexpr size_t RESPONSE_HEADER_LENGTH = 4U;
constexpr size_t TYPE_OFFSET = 0U;
constexpr size_t SOURCE_OFFSET = 1U;
constexpr size_t DESTINATION_OFFSET = 2U;
constexpr size_t TRANSACTION_ID_OFFSET = 3U;

/** Clamp traversal if a damaged registry advertises more physical slots. */
size_t safeRegistryCount(const SensorRegistry &registry)
{
    return registry.count <= MAX_SENSOR_NODES
               ? registry.count
               : MAX_SENSOR_NODES;
}

/**
 * Compare wrapping deadlines within the normal half-range timer window.
 * Gateway intervals and the 60-second backoff are far below INT32_MAX, so the
 * signed delta orders deadlines correctly even when uint32_t millis() wraps.
 */
bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs)
{
    return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

/**
 * Select the enabled node that has waited longest past its deadline.
 *
 * Scanning from nextPollIndex makes equal deadlines round-robin without
 * weakening the oldest-deadline priority. Due elapsed times are in the normal
 * half-range timer window, so unsigned subtraction also works across wrap.
 */
size_t findDueNodeIndex(const PollCoordinator &coordinator,
                        const SensorRegistry &registry,
                        uint32_t nowMs)
{
    const size_t count = safeRegistryCount(registry);
    if (count == 0U)
    {
        return MAX_SENSOR_NODES;
    }

    const size_t startIndex = coordinator.nextPollIndex % count;
    size_t selectedIndex = MAX_SENSOR_NODES;
    uint32_t longestOverdueMs = 0U;

    for (size_t offset = 0U; offset < count; ++offset)
    {
        const size_t index = (startIndex + offset) % count;
        if (registry.configs[index].enabled &&
            deadlineReached(nowMs, registry.states[index].nextPollAtMs))
        {
            const uint32_t overdueMs =
                nowMs - registry.states[index].nextPollAtMs;
            if (selectedIndex == MAX_SENSOR_NODES ||
                overdueMs > longestOverdueMs)
            {
                selectedIndex = index;
                longestOverdueMs = overdueMs;
            }
        }
    }

    return selectedIndex;
}

/** Locate the state/config slot bound to the transaction's node address. */
size_t findNodeIndex(const SensorRegistry &registry, uint8_t address)
{
    const size_t count = safeRegistryCount(registry);
    for (size_t index = 0U; index < count; ++index)
    {
        if (registry.configs[index].address == address)
        {
            return index;
        }
    }

    return MAX_SENSOR_NODES;
}

/** Add one to a 16-bit diagnostic counter without allowing rollover to zero. */
void incrementSaturating(uint16_t &value)
{
    if (value < UINT16_MAX)
    {
        ++value;
    }
}

/** Add one to consecutive failures while preserving the Offline state at max. */
void incrementSaturating(uint8_t &value)
{
    if (value < UINT8_MAX)
    {
        ++value;
    }
}

/**
 * Schedule one-node polling from the event that closed or accepted a request.
 * Unsigned addition intentionally wraps with the millisecond clock.
 */
void scheduleNextPoll(SensorRegistry &registry,
                      size_t nodeIndex,
                      uint32_t nowMs)
{
    registry.states[nodeIndex].nextPollAtMs =
        nowMs + registry.configs[nodeIndex].pollIntervalMs;
}

/** Apply the bounded offline backoff after the failure counter is updated. */
void scheduleNextPollAfterTimeout(SensorRegistry &registry,
                                  size_t nodeIndex,
                                  uint32_t nowMs)
{
    registry.states[nodeIndex].nextPollAtMs =
        nowMs + timeoutPollDelayMs(registry.configs[nodeIndex],
                                   registry.states[nodeIndex]
                                       .consecutiveFailures);
}

/** Clear fields that must not authorize a frame after a transaction closes. */
void closeTransaction(PollCoordinator &coordinator)
{
    coordinator.phase = CoordinatorPhase::Idle;
    coordinator.activeNodeAddress = GATEWAY_ADDRESS;
    coordinator.transactionId = 0U;
    coordinator.responseStartedAtMs = 0U;
    coordinator.acceptedResponse = PacketMessage{};
}

/** Construct a POLL and reserve the sole transaction slot until TX reports. */
CoordinatorAction beginPoll(PollCoordinator &coordinator,
                            const SensorRegistry &registry,
                            size_t nodeIndex)
{
    CoordinatorAction action{};
    action.type = CoordinatorActionType::SendPoll;
    action.packet.type = PacketType::Poll;
    action.packet.source = GATEWAY_ADDRESS;
    action.packet.destination = registry.configs[nodeIndex].address;
    action.packet.transactionId = coordinator.nextTransactionId;
    action.packet.payloadLength = 0U;
    action.packet.sequence = 0U;

    coordinator.phase = CoordinatorPhase::PollTxPending;
    coordinator.activeNodeAddress = registry.configs[nodeIndex].address;
    coordinator.transactionId = coordinator.nextTransactionId;
    coordinator.nextTransactionId =
        static_cast<uint8_t>(coordinator.nextTransactionId + 1U);
    const size_t count = safeRegistryCount(registry);
    coordinator.nextPollIndex = static_cast<uint8_t>((nodeIndex + 1U) % count);
    return action;
}

/** Produce the ACK fields mandated by V1 without performing radio I/O. */
CoordinatorAction makeAckAction(const PacketMessage &response)
{
    CoordinatorAction action{};
    action.type = CoordinatorActionType::SendAck;
    action.packet.type = PacketType::Ack;
    action.packet.source = GATEWAY_ADDRESS;
    action.packet.destination = response.source;
    action.packet.transactionId = response.transactionId;
    action.packet.payloadLength = 0U;
    action.packet.sequence = response.sequence;
    return action;
}

/**
 * Compare every logical response field retained by PacketMessage. This permits
 * only a true retry while waiting for ACK; a different response that happens to
 * reuse the same transaction and sequence cannot be acknowledged accidentally.
 */
bool responsesEqual(const PacketMessage &left, const PacketMessage &right)
{
    if (left.type != right.type ||
        left.source != right.source ||
        left.destination != right.destination ||
        left.transactionId != right.transactionId ||
        left.payloadLength != right.payloadLength ||
        left.sequence != right.sequence)
    {
        return false;
    }

    for (size_t index = 0U; index < left.payloadLength; ++index)
    {
        if (left.payload[index] != right.payload[index])
        {
            return false;
        }
    }

    return true;
}

/** Only the response belonging to the one active POLL may change node state. */
bool matchesActiveTransaction(const PollCoordinator &coordinator,
                              const PacketMessage &message)
{
    return (message.type == PacketType::Data ||
            message.type == PacketType::Error) &&
           message.source == coordinator.activeNodeAddress &&
           message.destination == GATEWAY_ADDRESS &&
           message.transactionId == coordinator.transactionId;
}

/**
 * Attribute a rejected frame only when its complete routing header identifies
 * the active DATA/ERROR transaction. Short frames and frames naming any other
 * source remain unattributed because their origin is not sufficiently clear.
 */
bool hasActiveResponseHeader(const PollCoordinator &coordinator,
                             const CoordinatorEvent &event)
{
    if (event.frame == nullptr ||
        event.frameLength < RESPONSE_HEADER_LENGTH)
    {
        return false;
    }

    const uint8_t type = event.frame[TYPE_OFFSET];
    return (type == static_cast<uint8_t>(PacketType::Data) ||
            type == static_cast<uint8_t>(PacketType::Error)) &&
           event.frame[SOURCE_OFFSET] == coordinator.activeNodeAddress &&
           event.frame[DESTINATION_OFFSET] == GATEWAY_ADDRESS &&
           event.frame[TRANSACTION_ID_OFFSET] == coordinator.transactionId;
}

/** Count one malformed frame without treating it as link-health evidence. */
void recordMalformedActiveResponse(const PollCoordinator &coordinator,
                                   SensorRegistry &registry,
                                   const CoordinatorEvent &event)
{
    if (!hasActiveResponseHeader(coordinator, event))
    {
        return;
    }

    const size_t nodeIndex =
        findNodeIndex(registry, coordinator.activeNodeAddress);
    if (nodeIndex < MAX_SENSOR_NODES)
    {
        incrementSaturating(registry.states[nodeIndex].packetErrorCount);
    }
}

/** Update measurements that prove the configured node was heard at this time. */
void updateLinkState(SensorNodeState &state, const CoordinatorEvent &event)
{
    state.lastSeenAtMs = event.nowMs;
    state.rssi = event.rssi;
    state.snr = static_cast<float>(event.snrX4) / 4.0F;
    state.consecutiveFailures = 0U;
}

/**
 * Count a response timeout, schedule the next attempt, and release the radio.
 * A POLL transmit failure does not call this helper because it never proves the
 * node failed to respond: the response window starts only after successful TX.
 */
void recordResponseTimeout(PollCoordinator &coordinator,
                           SensorRegistry &registry,
                           uint32_t nowMs)
{
    const size_t nodeIndex =
        findNodeIndex(registry, coordinator.activeNodeAddress);
    if (nodeIndex < MAX_SENSOR_NODES)
    {
        SensorNodeState &state = registry.states[nodeIndex];
        incrementSaturating(state.timeoutCount);
        incrementSaturating(state.consecutiveFailures);
        scheduleNextPollAfterTimeout(registry, nodeIndex, nowMs);
    }

    closeTransaction(coordinator);
}

/** Decode a frame into a local object so rejection cannot expose partial data. */
bool decodeReceivedFrame(const CoordinatorEvent &event,
                         PacketMessage &message)
{
    return event.type == CoordinatorEventType::FrameReceived &&
           decodePacket(event.frame, event.frameLength, &message);
}

/**
 * Accept a first response, update dedup/counters, and request its ACK. All
 * payload extraction finishes before registry mutation even though decodePacket
 * has already enforced the wire-level payload contract.
 */
CoordinatorAction acceptResponse(PollCoordinator &coordinator,
                                 SensorRegistry &registry,
                                 const CoordinatorEvent &event,
                                 const PacketMessage &message)
{
    int16_t temperatureX10 = 0;
    uint16_t humidityX10 = 0U;
    PacketErrorCode validatedErrorCode = PacketErrorCode::DhtInitFailed;

    if (message.type == PacketType::Data &&
        !decodeDataPayload(&message, &temperatureX10, &humidityX10))
    {
        return CoordinatorAction{};
    }

    if (message.type == PacketType::Error &&
        !decodeErrorPayload(&message, &validatedErrorCode))
    {
        return CoordinatorAction{};
    }

    const size_t nodeIndex =
        findNodeIndex(registry, coordinator.activeNodeAddress);
    if (nodeIndex >= MAX_SENSOR_NODES)
    {
        closeTransaction(coordinator);
        return CoordinatorAction{};
    }

    SensorNodeState &state = registry.states[nodeIndex];
    updateLinkState(state, event);
    scheduleNextPoll(registry, nodeIndex, event.nowMs);

    CoordinatorAction action = makeAckAction(message);
    const bool isNewSequence =
        !state.hasSequence || state.lastSequence != message.sequence;

    if (isNewSequence)
    {
        state.lastSequence = message.sequence;
        state.hasSequence = true;

        if (message.type == PacketType::Error)
        {
            incrementSaturating(state.sensorErrorCount);
        }
        else if (temperatureX10 < MIN_TEMPERATURE_X10 ||
                 temperatureX10 > MAX_TEMPERATURE_X10 ||
                 humidityX10 > MAX_HUMIDITY_X10)
        {
            incrementSaturating(state.packetErrorCount);
        }
        else
        {
            incrementSaturating(state.successCount);
            action.hasSample = true;
            action.sample.nodeId = message.source;
            action.sample.sequence = message.sequence;
            action.sample.temperatureX10 = temperatureX10;
            action.sample.humidityX10 = humidityX10;
            action.sample.rssi = event.rssi;
            action.sample.snrX4 = event.snrX4;
            action.sample.receivedAtMs = event.nowMs;
        }
    }

    // Retaining the validated response authorizes only an identical retry while
    // the adapter is still completing this ACK.
    coordinator.acceptedResponse = message;
    coordinator.phase = CoordinatorPhase::AckTxPending;
    return action;
}

/** Re-ACK an identical retry while leaving sequence and counters unchanged. */
CoordinatorAction handleAckPendingFrame(PollCoordinator &coordinator,
                                        SensorRegistry &registry,
                                        const CoordinatorEvent &event)
{
    PacketMessage message{};
    if (!decodeReceivedFrame(event, message))
    {
        recordMalformedActiveResponse(coordinator, registry, event);
        return CoordinatorAction{};
    }

    if (!responsesEqual(message, coordinator.acceptedResponse))
    {
        return CoordinatorAction{};
    }

    const size_t nodeIndex =
        findNodeIndex(registry, coordinator.activeNodeAddress);
    if (nodeIndex >= MAX_SENSOR_NODES)
    {
        closeTransaction(coordinator);
        return CoordinatorAction{};
    }

    // The retransmission is fresh evidence about the radio link, but it is the
    // same sensor response, so content counters and the queued sample stay put.
    updateLinkState(registry.states[nodeIndex], event);
    return makeAckAction(coordinator.acceptedResponse);
}
} // namespace

void initializePollCoordinator(PollCoordinator *coordinator,
                               uint8_t firstTransactionId)
{
    if (coordinator == nullptr)
    {
        return;
    }

    *coordinator = PollCoordinator{};
    coordinator->nextTransactionId = firstTransactionId;
}

CoordinatorAction handleCoordinatorEvent(PollCoordinator *coordinator,
                                         SensorRegistry *registry,
                                         const CoordinatorEvent *event)
{
    if (coordinator == nullptr || registry == nullptr || event == nullptr)
    {
        return CoordinatorAction{};
    }

    // Test timeout before handling a frame so an arrival at exactly 1000 ms is
    // rejected and cannot update link state or request an ACK.
    if (coordinator->phase == CoordinatorPhase::WaitingForResponse &&
        event->nowMs - coordinator->responseStartedAtMs >= RESPONSE_TIMEOUT_MS)
    {
        recordResponseTimeout(*coordinator, *registry, event->nowMs);
        return CoordinatorAction{};
    }

    switch (coordinator->phase)
    {
    case CoordinatorPhase::Idle:
        if (event->type == CoordinatorEventType::Tick)
        {
            const size_t nodeIndex =
                findDueNodeIndex(*coordinator, *registry, event->nowMs);
            if (nodeIndex < MAX_SENSOR_NODES)
            {
                return beginPoll(*coordinator, *registry, nodeIndex);
            }
        }
        break;

    case CoordinatorPhase::PollTxPending:
        if (event->type == CoordinatorEventType::PollTxSucceeded)
        {
            coordinator->responseStartedAtMs = event->nowMs;
            coordinator->phase = CoordinatorPhase::WaitingForResponse;
        }
        else if (event->type == CoordinatorEventType::PollTxFailed)
        {
            const size_t nodeIndex =
                findNodeIndex(*registry, coordinator->activeNodeAddress);
            if (nodeIndex < MAX_SENSOR_NODES)
            {
                scheduleNextPoll(*registry, nodeIndex, event->nowMs);
            }
            closeTransaction(*coordinator);
        }
        break;

    case CoordinatorPhase::WaitingForResponse:
        if (event->type == CoordinatorEventType::FrameReceived)
        {
            PacketMessage message{};
            if (decodeReceivedFrame(*event, message))
            {
                if (matchesActiveTransaction(*coordinator, message))
                {
                    return acceptResponse(
                        *coordinator, *registry, *event, message);
                }
            }
            else
            {
                recordMalformedActiveResponse(
                    *coordinator, *registry, *event);
            }
        }
        break;

    case CoordinatorPhase::AckTxPending:
        if (event->type == CoordinatorEventType::AckTxSucceeded ||
            event->type == CoordinatorEventType::AckTxFailed)
        {
            closeTransaction(*coordinator);
        }
        else if (event->type == CoordinatorEventType::FrameReceived)
        {
            return handleAckPendingFrame(*coordinator, *registry, *event);
        }
        break;

    default:
        // A corrupted phase cannot authorize I/O or registry mutation. Reset to
        // Idle so the next tick can recover through the normal scheduling path.
        closeTransaction(*coordinator);
        break;
    }

    return CoordinatorAction{};
}
} // namespace gateway
