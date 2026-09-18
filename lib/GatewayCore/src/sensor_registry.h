#pragma once

#include "gateway_model.h"

namespace gateway
{
/**
 * Validate a complete build-time node list and initialize its runtime state.
 *
 * The list must contain 1..MAX_SENSOR_NODES unique sensor addresses. Every
 * entry is validated even when disabled, and every interval must respect the
 * sensor cooldown. Enabled nodes are due immediately at nowMs; the scheduler
 * will skip disabled nodes later.
 *
 * A temporary registry is built first. If validation fails, the registry owned
 * by the caller remains unchanged and the returned enum identifies the reason.
 */
RegistryInitResult initializeSensorRegistry(SensorRegistry *registry,
                                            const SensorNodeConfig *configs,
                                            size_t configCount,
                                            uint32_t nowMs);

/**
 * Find immutable configuration by LoRa address.
 * Returns nullptr for a null registry or an address that is not configured.
 */
const SensorNodeConfig *findNodeConfig(const SensorRegistry *registry,
                                       uint8_t address);

/**
 * Find mutable runtime state by LoRa address.
 * Configuration and state share an index, so changes affect only that node.
 */
SensorNodeState *findNodeState(SensorRegistry *registry, uint8_t address);

/** Const overload for diagnostics that must not modify runtime node state. */
const SensorNodeState *findNodeState(const SensorRegistry *registry,
                                     uint8_t address);

/**
 * Convert raw response history into the four operator-facing health states.
 * NeverSeen takes precedence; otherwise 0 failures is Online, 1..2 is
 * Degraded, and 3 or more is Offline.
 */
NodeHealth deriveNodeHealth(const SensorNodeState &state);

/** Return a stable, allocation-free diagnostic label for initialization logs. */
const char *registryInitResultName(RegistryInitResult result);
} // namespace gateway
