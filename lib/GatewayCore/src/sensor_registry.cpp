#include "sensor_registry.h"
#include <string.h>
namespace gateway
{
bool isValidDeviceName(const char *name)
{
    if (name == nullptr || name[0] == '\0') return false;
    for (size_t i = 0U; i < DEVICE_NAME_CAPACITY; ++i)
    {
        const char c = name[i];
        if (c == '\0') return true;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return false;
}
RegistryInitResult initializeSensorRegistry(SensorRegistry *registry,
    const SensorNodeConfig *configs, size_t configCount)
{
    if (registry == nullptr || configs == nullptr) return RegistryInitResult::NullArgument;
    if (configCount == 0U || configCount > MAX_SENSOR_NODES) return RegistryInitResult::InvalidCount;
    SensorRegistry candidate{};
    for (size_t i = 0U; i < configCount; ++i)
    {
        const SensorNodeConfig &config = configs[i];
        if (config.address == 0U || config.address == 255U) return RegistryInitResult::InvalidAddress;
        if (!isValidDeviceName(config.deviceName)) return RegistryInitResult::InvalidDeviceName;
        for (size_t j = 0U; j < i; ++j)
        {
            if (candidate.configs[j].address == config.address) return RegistryInitResult::DuplicateAddress;
            if (strcmp(candidate.configs[j].deviceName, config.deviceName) == 0) return RegistryInitResult::DuplicateDeviceName;
        }
        candidate.configs[i] = config;
        ++candidate.count;
    }
    *registry = candidate;
    return RegistryInitResult::Ok;
}
const SensorNodeConfig *findNodeConfig(const SensorRegistry *registry, uint8_t address)
{
    if (registry == nullptr || registry->count > MAX_SENSOR_NODES) return nullptr;
    for (size_t i = 0U; i < registry->count; ++i)
        if (registry->configs[i].address == address) return &registry->configs[i];
    return nullptr;
}
const char *registryInitResultName(RegistryInitResult result)
{
    switch (result)
    {
    case RegistryInitResult::Ok: return "ok";
    case RegistryInitResult::NullArgument: return "null_argument";
    case RegistryInitResult::InvalidCount: return "invalid_count";
    case RegistryInitResult::InvalidAddress: return "invalid_address";
    case RegistryInitResult::DuplicateAddress: return "duplicate_address";
    case RegistryInitResult::InvalidDeviceName: return "invalid_device_name";
    case RegistryInitResult::DuplicateDeviceName: return "duplicate_device_name";
    default: return "unknown";
    }
}
} // namespace gateway
