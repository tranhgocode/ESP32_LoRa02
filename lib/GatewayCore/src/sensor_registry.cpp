#include "sensor_registry.h"

namespace gateway
{
namespace
{
/** Gateway and reserved addresses can never identify a physical sensor. */
bool isNodeAddress(uint8_t address)
{
    return address >= 0x01U && address <= 0xFEU;
}

/**
 * Search only entries already copied into the candidate registry. During
 * initialization this detects a duplicate before the caller's registry is
 * modified.
 */
bool containsAddress(const SensorRegistry &registry, uint8_t address)
{
    for (size_t index = 0U; index < registry.count; ++index)
    {
        if (registry.configs[index].address == address)
        {
            return true;
        }
    }

    return false;
}

/**
 * Limit traversal to physical array capacity. A valid registry always meets
 * this condition, while the clamp prevents accidental out-of-bounds access if
 * a caller passes a partially corrupted object to a lookup function.
 */
size_t safeRegistryCount(const SensorRegistry &registry)
{
    return registry.count <= MAX_SENSOR_NODES
               ? registry.count
               : MAX_SENSOR_NODES;
}
} // namespace

RegistryInitResult initializeSensorRegistry(SensorRegistry *registry,
                                            const SensorNodeConfig *configs,
                                            size_t configCount,
                                            uint32_t nowMs)
{
    if (registry == nullptr || configs == nullptr)
    {
        return RegistryInitResult::NullArgument;
    }

    if (configCount == 0U || configCount > MAX_SENSOR_NODES)
    {
        return RegistryInitResult::InvalidCount;
    }

    // All writes go to this value-initialized candidate. Assigning it at the
    // end gives initialization transaction-like all-or-nothing behavior.
    SensorRegistry candidate{};

    for (size_t index = 0U; index < configCount; ++index)
    {
        const SensorNodeConfig &config = configs[index];

        if (!isNodeAddress(config.address))
        {
            return RegistryInitResult::InvalidAddress;
        }

        if (config.pollIntervalMs < MIN_POLL_INTERVAL_MS)
        {
            return RegistryInitResult::PollIntervalTooShort;
        }

        if (containsAddress(candidate, config.address))
        {
            return RegistryInitResult::DuplicateAddress;
        }

        candidate.configs[index] = config;
        candidate.states[index].nextPollAtMs = nowMs;
        ++candidate.count;
    }

    *registry = candidate;
    return RegistryInitResult::Ok;
}

const SensorNodeConfig *findNodeConfig(const SensorRegistry *registry,
                                       uint8_t address)
{
    if (registry == nullptr)
    {
        return nullptr;
    }

    const size_t count = safeRegistryCount(*registry);
    for (size_t index = 0U; index < count; ++index)
    {
        if (registry->configs[index].address == address)
        {
            return &registry->configs[index];
        }
    }

    return nullptr;
}

SensorNodeState *findNodeState(SensorRegistry *registry, uint8_t address)
{
    if (registry == nullptr)
    {
        return nullptr;
    }

    const size_t count = safeRegistryCount(*registry);
    for (size_t index = 0U; index < count; ++index)
    {
        // State uses the same index as its immutable configuration. Returning
        // that one slot prevents updates for one node from touching another.
        if (registry->configs[index].address == address)
        {
            return &registry->states[index];
        }
    }

    return nullptr;
}

const SensorNodeState *findNodeState(const SensorRegistry *registry,
                                     uint8_t address)
{
    if (registry == nullptr)
    {
        return nullptr;
    }

    const size_t count = safeRegistryCount(*registry);
    for (size_t index = 0U; index < count; ++index)
    {
        if (registry->configs[index].address == address)
        {
            return &registry->states[index];
        }
    }

    return nullptr;
}

NodeHealth deriveNodeHealth(const SensorNodeState &state)
{
    if (!state.hasSequence)
    {
        return NodeHealth::NeverSeen;
    }

    if (state.consecutiveFailures == 0U)
    {
        return NodeHealth::Online;
    }

    if (state.consecutiveFailures < 3U)
    {
        return NodeHealth::Degraded;
    }

    return NodeHealth::Offline;
}

uint32_t timeoutPollDelayMs(const SensorNodeConfig &config,
                            uint8_t consecutiveFailures)
{
    if (consecutiveFailures < 3U)
    {
        return config.pollIntervalMs;
    }

    uint32_t delayMs = config.pollIntervalMs;
    uint8_t doublings = static_cast<uint8_t>(consecutiveFailures - 2U);
    while (doublings > 0U)
    {
        // Returning the cap before multiplying prevents both arithmetic
        // overflow and unnecessary work for a saturated failure counter.
        if (delayMs >= MAX_OFFLINE_POLL_INTERVAL_MS / 2U)
        {
            return MAX_OFFLINE_POLL_INTERVAL_MS;
        }

        delayMs *= 2U;
        --doublings;
    }

    return delayMs;
}

const char *registryInitResultName(RegistryInitResult result)
{
    switch (result)
    {
    case RegistryInitResult::Ok:
        return "ok";
    case RegistryInitResult::NullArgument:
        return "null_argument";
    case RegistryInitResult::InvalidCount:
        return "invalid_count";
    case RegistryInitResult::InvalidAddress:
        return "invalid_address";
    case RegistryInitResult::DuplicateAddress:
        return "duplicate_address";
    case RegistryInitResult::PollIntervalTooShort:
        return "poll_interval_too_short";
    default:
        return "unknown";
    }
}
} // namespace gateway
