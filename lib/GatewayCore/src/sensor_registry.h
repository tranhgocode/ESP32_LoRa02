#pragma once
#include "gateway_model.h"
namespace gateway
{
bool isValidDeviceName(const char *name);
RegistryInitResult initializeSensorRegistry(SensorRegistry *registry,
    const SensorNodeConfig *configs, size_t configCount);
const SensorNodeConfig *findNodeConfig(const SensorRegistry *registry, uint8_t address);
const char *registryInitResultName(RegistryInitResult result);
} // namespace gateway
