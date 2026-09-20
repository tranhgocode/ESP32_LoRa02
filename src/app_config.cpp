#include "app_config.h"

#if GATEWAY_V1_ENABLED
namespace
{
/** Construct one node entry without relying on C++ aggregate initialization. */
gateway::SensorNodeConfig makeSensorNode(uint8_t address,
                                         uint32_t pollIntervalMs,
                                         bool enabled)
{
    gateway::SensorNodeConfig config{};
    config.address = address;
    config.pollIntervalMs = pollIntervalMs;
    config.enabled = enabled;
    return config;
}
} // namespace

namespace gateway_config
{
// Address 0x01 is the first deployable sensor identity. A ten-second interval
// follows the project contract and remains above the DHT11 cooldown minimum.
const gateway::SensorNodeConfig SENSOR_NODES[] = {
    makeSensorNode(0x01U, 10000U, true),
    makeSensorNode(0x02U, 10000U, true),
};

// Derive the count from the array so adding or removing a configuration cannot
// leave initializeSensorRegistry() with a stale manual length.
const size_t SENSOR_NODE_COUNT =
    sizeof(SENSOR_NODES) / sizeof(SENSOR_NODES[0]);

static_assert(sizeof(SENSOR_NODES) / sizeof(SENSOR_NODES[0]) <=
                  gateway::MAX_SENSOR_NODES,
              "Configured sensor count exceeds gateway capacity");
} // namespace gateway_config
#endif
