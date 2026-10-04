#include "telemetry_delivery.h"

#include "telemetry_json.h"

namespace gateway
{
TelemetryDeliveryResult deliverNextTelemetry(
    TelemetryBuffer *buffer,
    TelemetryPublishFunction publish,
    void *publishContext)
{
    if (buffer == nullptr || publish == nullptr)
    {
        return TelemetryDeliveryResult::InvalidArgument;
    }

    const TelemetrySample *sample = peekTelemetrySample(buffer);
    if (sample == nullptr)
    {
        return TelemetryDeliveryResult::QueueEmpty;
    }

    char payload[TELEMETRY_JSON_CAPACITY] = {};
    size_t payloadLength = 0U;
    if (!serializeTelemetryJson(
            sample, payload, sizeof(payload), &payloadLength))
    {
        return TelemetryDeliveryResult::EncodingFailed;
    }

    if (!publish(payload, payloadLength, publishContext))
    {
        return TelemetryDeliveryResult::PublishRejected;
    }

    if (!popTelemetrySample(buffer))
    {
        return TelemetryDeliveryResult::InvalidArgument;
    }

    return TelemetryDeliveryResult::Delivered;
}

TelemetryDeliveryResult serviceTelemetryDelivery(
    TelemetryBuffer *buffer,
    TelemetryPublishFunction publish,
    void *publishContext,
    TelemetryDeliveryService *service,
    uint32_t nowMs)
{
    if (service == nullptr)
    {
        return TelemetryDeliveryResult::InvalidArgument;
    }

    if (service->retryPending &&
        static_cast<uint32_t>(nowMs - service->retryAtMs) >= 0x80000000UL)
    {
        return TelemetryDeliveryResult::RetryDeferred;
    }

    const TelemetryDeliveryResult result = deliverNextTelemetry(
        buffer, publish, publishContext);
    if (result == TelemetryDeliveryResult::PublishRejected ||
        result == TelemetryDeliveryResult::EncodingFailed)
    {
        service->retryPending = true;
        service->retryAtMs = nowMs + TELEMETRY_DELIVERY_RETRY_INTERVAL_MS;
    }
    else
    {
        service->retryPending = false;
    }

    return result;
}
} // namespace gateway
