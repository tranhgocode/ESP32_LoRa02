#pragma once

#include "telemetry_sample.h"
#include "sensor_registry.h"

#include <stddef.h>

namespace gateway
{
// Includes the terminator and the longest device name with all optional fields.
constexpr size_t TELEMETRY_JSON_CAPACITY = 320U;

/**
 * Serialize one telemetry sample to the gateway MQTT JSON schema.
 *
 * Includes the base fields and available PM2.5. Complete forecasts appear at
 * sequence numbers 23, 47, 71, and every 24 sequences thereafter.
 * Temperature and humidity have one decimal digit and SNR has two.
 *
 * Output and length stay unchanged on failure. On success the output is
 * null-terminated and the returned length excludes the terminator.
 */
bool serializeTelemetryJson(const TelemetrySample *sample,
                            char *output,
                            size_t outputCapacity,
                            size_t *outputLength);
/** Wrap a sample under the device name and add batteryLevel when calibrated. */
bool serializeGatewayTelemetryJson(const TelemetrySample *sample, const SensorRegistry *registry,
                                   char *output, size_t capacity, size_t *length);
bool serializeGatewayConnectJson(const SensorNodeConfig *config, char *output, size_t capacity, size_t *length);
} // namespace gateway
