#pragma once

#include "telemetry_sample.h"

#include <stddef.h>

namespace gateway
{
// Capacity includes the terminating null byte. It is large enough for every
// numeric extreme representable by TelemetrySample and keeps stack use bounded.
constexpr size_t TELEMETRY_JSON_CAPACITY = 128U;

/**
 * Serialize one telemetry sample to the gateway MQTT JSON schema.
 *
 * The output contains node_id, sequence, temperature, humidity, rssi, and snr
 * in that stable order. Fixed-point source values are formatted with integer
 * arithmetic: temperature and humidity have one decimal digit, while SNR has
 * two digits so quarter-dB values remain exact. receivedAtMs is internal queue
 * metadata and is intentionally not published by the V1 schema.
 *
 * JSON is first built in a fixed local buffer and copied only after snprintf
 * succeeds and the caller's capacity is sufficient. Therefore output and
 * outputLength remain unchanged on null arguments, formatting failure, or
 * truncation. A successful output is null-terminated and outputLength excludes
 * that terminator.
 */
bool serializeTelemetryJson(const TelemetrySample *sample,
                            char *output,
                            size_t outputCapacity,
                            size_t *outputLength);
} // namespace gateway
