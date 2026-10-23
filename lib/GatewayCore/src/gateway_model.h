#pragma once
#include <stddef.h>
#include <stdint.h>
namespace gateway
{
constexpr size_t MAX_SENSOR_NODES = 5U;
constexpr size_t DEVICE_NAME_CAPACITY = 48U;
struct SensorNodeConfig
{
    uint8_t address = 0U;
    char deviceName[DEVICE_NAME_CAPACITY] = {};
    bool enabled = false;
};
enum class RegistryInitResult : uint8_t
{
    Ok = 0U, NullArgument, InvalidCount, InvalidAddress, DuplicateAddress,
    InvalidDeviceName, DuplicateDeviceName
};
struct SensorRegistry
{
    SensorNodeConfig configs[MAX_SENSOR_NODES] = {};
    size_t count = 0U;
};
} // namespace gateway
