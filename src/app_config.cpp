#include "app_config.h"
#include <string.h>
#include "sensor_registry.h"
namespace gateway_config
{
namespace
{
gateway::SensorNodeConfig makeNode(uint8_t address, const char *name)
{
    gateway::SensorNodeConfig config{};
    config.address = address;
    // Leave an invalid empty name for registry validation instead of truncating.
    if (gateway::isValidDeviceName(name))
    {
        memcpy(config.deviceName, name, strlen(name) + 1U);
    }
    config.enabled = true;
    return config;
}
}
const gateway::SensorNodeConfig SENSOR_NODES[] = {
    makeNode(0x01U, "lora-gw01-node01"),
    makeNode(0x02U, "lora-gw01-node02")
};
const size_t SENSOR_NODE_COUNT = sizeof(SENSOR_NODES) / sizeof(SENSOR_NODES[0]);
static_assert(SENSOR_NODE_COUNT <= gateway::MAX_SENSOR_NODES, "Too many nodes");
}
