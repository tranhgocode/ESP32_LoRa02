#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gateway
{
// The first gateway version deliberately uses a small fixed registry. Fixed
// storage makes memory use predictable and avoids heap allocation in the loop.
constexpr size_t MAX_SENSOR_NODES = 5U;
constexpr uint32_t MIN_POLL_INTERVAL_MS = 1500U;

/** Health shown to operators, derived from runtime fields instead of stored. */
enum class NodeHealth : uint8_t
{
    NeverSeen = 0U,
    Online,
    Degraded,
    Offline
};

/** Build-time configuration for one sensor node. */
struct SensorNodeConfig
{
    uint8_t address = 0U;
    uint32_t pollIntervalMs = 0U;
    bool enabled = false;
};

/**
 * Mutable observations and counters belonging to one configured node.
 *
 * RSSI uses the radio driver's integer dBm value and SNR keeps the driver's
 * floating-point value. hasSequence also answers whether any valid DATA or
 * ERROR response has ever been accepted, which makes NeverSeen unambiguous
 * even when a response arrives at millis() == 0.
 */
struct SensorNodeState
{
    uint32_t nextPollAtMs = 0U;
    uint32_t lastSeenAtMs = 0U;
    uint16_t lastSequence = 0U;
    uint16_t successCount = 0U;
    uint16_t timeoutCount = 0U;
    uint16_t packetErrorCount = 0U;
    uint16_t sensorErrorCount = 0U;
    uint8_t consecutiveFailures = 0U;
    int rssi = 0;
    float snr = 0.0F;
    bool hasSequence = false;
};

/** Result codes let the hardware-facing startup code log a precise failure. */
enum class RegistryInitResult : uint8_t
{
    Ok = 0U,
    NullArgument,
    InvalidCount,
    InvalidAddress,
    DuplicateAddress,
    PollIntervalTooShort
};

/**
 * Fixed-capacity registry pairing each configuration with state at the same
 * array index. count tells consumers which entries are active; unused entries
 * stay value-initialized and consume no dynamic memory.
 */
struct SensorRegistry
{
    SensorNodeConfig configs[MAX_SENSOR_NODES] = {};
    SensorNodeState states[MAX_SENSOR_NODES] = {};
    size_t count = 0U;
};
} // namespace gateway
