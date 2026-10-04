#pragma once

#include "telemetry_buffer.h"

#include <stddef.h>
#include <stdint.h>

namespace gateway
{
/** Outcome of one attempt to hand the FIFO head to a publisher. */
enum class TelemetryDeliveryResult : uint8_t
{
    InvalidArgument = 0U,
    QueueEmpty,
    EncodingFailed,
    PublishRejected,
    RetryDeferred,
    Delivered
};

/** State for wrap-safe retry spacing after a publisher rejects the FIFO head. */
struct TelemetryDeliveryService
{
    bool retryPending = false;
    uint32_t retryAtMs = 0U;
};

constexpr uint32_t TELEMETRY_DELIVERY_RETRY_INTERVAL_MS = 1000U;

/**
 * MQTT boundary used by the portable delivery logic.
 *
 * Returning true transfers ownership of this payload to the publisher. The
 * payload bytes are valid only for the duration of the call, so an async
 * adapter must copy them before returning true. The callback can block; the
 * application must schedule it where that wait cannot interrupt radio work.
 */
using TelemetryPublishFunction = bool (*)(const char *payload,
                                          size_t payloadLength,
                                          void *context);

/**
 * Serialize and offer at most the oldest queued sample to a publisher.
 *
 * The FIFO head is removed only after the publisher accepts the complete JSON
 * payload. Empty queues and failures leave the buffer unchanged, allowing the
 * caller to retry on a later service iteration without reordering samples.
 */
TelemetryDeliveryResult deliverNextTelemetry(
    TelemetryBuffer *buffer,
    TelemetryPublishFunction publish,
    void *publishContext);

/**
 * Service at most one queue item. Rejections schedule the next attempt one
 * second later; successful sends may continue on the next loop iteration.
 */
TelemetryDeliveryResult serviceTelemetryDelivery(
    TelemetryBuffer *buffer,
    TelemetryPublishFunction publish,
    void *publishContext,
    TelemetryDeliveryService *service,
    uint32_t nowMs);
} // namespace gateway
