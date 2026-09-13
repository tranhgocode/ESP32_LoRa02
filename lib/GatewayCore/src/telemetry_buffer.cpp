#include "telemetry_buffer.h"

#include <stdint.h>

namespace gateway
{
namespace
{
/** Advance one ring index and wrap exactly at the fixed array capacity. */
size_t nextIndex(size_t index)
{
    ++index;
    return index == TELEMETRY_BUFFER_CAPACITY ? 0U : index;
}

/** Increment a diagnostic counter without allowing unsigned wrap to zero. */
void incrementSaturating(uint32_t *counter)
{
    if (*counter < UINT32_MAX)
    {
        ++(*counter);
    }
}
} // namespace

size_t telemetryBufferSize(const TelemetryBuffer *buffer)
{
    return buffer == nullptr ? 0U : buffer->count;
}

bool telemetryBufferIsEmpty(const TelemetryBuffer *buffer)
{
    return buffer == nullptr || buffer->count == 0U;
}

bool pushTelemetrySample(TelemetryBuffer *buffer,
                         const TelemetrySample *sample)
{
    if (buffer == nullptr || sample == nullptr)
    {
        return false;
    }

    if (buffer->count == TELEMETRY_BUFFER_CAPACITY)
    {
        // In a full ring, head is also the next write slot. Replacing it drops
        // exactly the oldest sample; moving head then makes the following item
        // the oldest and leaves the newly-written item at the logical tail.
        buffer->samples[buffer->head] = *sample;
        buffer->head = nextIndex(buffer->head);
        incrementSaturating(&buffer->telemetryDropped);
        return true;
    }

    // count is smaller than capacity, so head + count is at most 126. One
    // subtraction is therefore enough to map the logical tail into 0..63.
    size_t tail = buffer->head + buffer->count;
    if (tail >= TELEMETRY_BUFFER_CAPACITY)
    {
        tail -= TELEMETRY_BUFFER_CAPACITY;
    }

    buffer->samples[tail] = *sample;
    ++buffer->count;
    return true;
}

const TelemetrySample *peekTelemetrySample(const TelemetryBuffer *buffer)
{
    if (buffer == nullptr || buffer->count == 0U)
    {
        return nullptr;
    }

    return &buffer->samples[buffer->head];
}

bool popTelemetrySample(TelemetryBuffer *buffer)
{
    if (buffer == nullptr || buffer->count == 0U)
    {
        return false;
    }

    buffer->head = nextIndex(buffer->head);
    --buffer->count;

    // Canonicalize an empty queue so the next push starts at slot zero. This is
    // not required for correctness, but it makes empty state deterministic for
    // diagnostics and avoids retaining an arbitrary wrapped head index.
    if (buffer->count == 0U)
    {
        buffer->head = 0U;
    }

    return true;
}

void resetTelemetryBuffer(TelemetryBuffer *buffer)
{
    if (buffer == nullptr)
    {
        return;
    }

    buffer->head = 0U;
    buffer->count = 0U;
    buffer->telemetryDropped = 0U;
}
} // namespace gateway
