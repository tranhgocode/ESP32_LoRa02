#pragma once

#include "telemetry_sample.h"

#include <stddef.h>
#include <stdint.h>

namespace gateway
{
// Sixty-four samples give the V1 gateway a bounded offline backlog while
// keeping RAM use predictable on the ESP32.
constexpr size_t TELEMETRY_BUFFER_CAPACITY = 64U;

/**
 * Fixed-capacity FIFO storage for telemetry waiting to be handed to MQTT.
 *
 * head points to the oldest readable sample and count describes how many
 * entries are currently valid. telemetryDropped is public diagnostic state;
 * callers should mutate the queue itself only through the functions below so
 * the ring invariants remain valid. Value initialization creates an empty,
 * ready-to-use queue without heap allocation or a separate init function.
 */
struct TelemetryBuffer
{
    TelemetrySample samples[TELEMETRY_BUFFER_CAPACITY] = {};
    size_t head = 0U;
    size_t count = 0U;
    uint32_t telemetryDropped = 0U;
};

/** Return the number of readable samples, or zero for a null buffer. */
size_t telemetryBufferSize(const TelemetryBuffer *buffer);

/** Treat a null buffer as empty so diagnostic callers can fail safely. */
bool telemetryBufferIsEmpty(const TelemetryBuffer *buffer);

/**
 * Append a sample to the FIFO without allocating memory.
 *
 * When full, the function replaces the oldest sample, advances head, retains
 * the new sample, and increments telemetryDropped with saturation at
 * UINT32_MAX. It returns false only when either pointer is null.
 */
bool pushTelemetrySample(TelemetryBuffer *buffer,
                         const TelemetrySample *sample);

/**
 * Return a read-only pointer to the oldest sample without removing it.
 *
 * The pointer remains valid until the buffer is mutated by push, pop, or reset.
 * A null or empty buffer returns nullptr.
 */
const TelemetrySample *peekTelemetrySample(const TelemetryBuffer *buffer);

/**
 * Remove the oldest sample after a downstream consumer accepts it.
 * Returns false for a null or empty buffer and leaves all state unchanged.
 */
bool popTelemetrySample(TelemetryBuffer *buffer);

/**
 * Make the queue reusable from its initial state.
 *
 * Reset clears the logical contents and drop counter. Stored bytes need not be
 * erased because count becomes zero and no API can read an inactive slot.
 * Passing nullptr is a safe no-op.
 */
void resetTelemetryBuffer(TelemetryBuffer *buffer);
} // namespace gateway
