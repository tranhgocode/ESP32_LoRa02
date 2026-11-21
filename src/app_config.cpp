#include "app_config.h"
#include <string.h>
#include "sensor_registry.h"
namespace gateway_config
{
namespace
{
// Estimate the displayed charge from voltage measured before the buck converter.
// The 3300 mV endpoint is provisional until measured on this node under load.
constexpr gateway::BatteryCurvePoint NODE02_BATTERY_POINTS[] = {
    {3300U, 0U},
    {4200U, 100U}
};
constexpr size_t NODE02_BATTERY_POINT_COUNT =
    sizeof(NODE02_BATTERY_POINTS) / sizeof(NODE02_BATTERY_POINTS[0]);

gateway::SensorNodeConfig makeNode(uint8_t address, const char *name,
                                  const gateway::BatteryCurvePoint *batteryPoints = nullptr,
                                  size_t batteryPointCount = 0U)
{
    gateway::SensorNodeConfig config{};
    config.address = address;
    // Leave an invalid empty name for registry validation instead of truncating.
    if (gateway::isValidDeviceName(name))
    {
        memcpy(config.deviceName, name, strlen(name) + 1U);
    }
    config.enabled = true;
    config.batteryCurve.points = batteryPoints;
    config.batteryCurve.count = batteryPointCount;
    return config;
}
}
const gateway::SensorNodeConfig SENSOR_NODES[] = {
    makeNode(0x01U, "lora-gw01-node01"),
    makeNode(0x02U, "lora-gw01-node02", NODE02_BATTERY_POINTS, NODE02_BATTERY_POINT_COUNT)
};
const size_t SENSOR_NODE_COUNT = sizeof(SENSOR_NODES) / sizeof(SENSOR_NODES[0]);
static_assert(SENSOR_NODE_COUNT <= gateway::MAX_SENSOR_NODES, "Too many nodes");
}
