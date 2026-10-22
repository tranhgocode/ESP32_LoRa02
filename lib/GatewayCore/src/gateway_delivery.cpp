#include "gateway_delivery.h"
#include "telemetry_json.h"
namespace gateway
{
namespace
{
struct Publisher
{
    GatewayPublishFunction publish;
    void *context;
};
bool publishTelemetry(const char *payload, size_t length, void *context)
{
    Publisher *publisher = static_cast<Publisher *>(context);
    return publisher->publish(GATEWAY_TELEMETRY_TOPIC, payload, length, publisher->context);
}
}
void serviceGatewayDelivery(GatewayDelivery *service, const SensorRegistry *registry,
    TelemetryBuffer *buffer, const MqttSession &session, GatewayPublishFunction publish,
    void *context, uint32_t nowMs)
{
    if (service == nullptr || registry == nullptr || buffer == nullptr || publish == nullptr ||
        registry->count > MAX_SENSOR_NODES) return;
    if (!session.connected)
    {
        *service = GatewayDelivery{};
        return;
    }
    if (service->generation != session.generation)
    {
        *service = GatewayDelivery{};
        service->generation = session.generation;
    }
    if (session.registrationLost) service->waitingForAck = false;
    if (service->waitingForAck)
    {
        if (session.registrationAcknowledged)
        {
            service->waitingForAck = false;
            ++service->registrationIndex;
        }
        else if (static_cast<uint32_t>(nowMs - service->ackDeadlineMs) < 0x80000000UL)
        {
            // The SDK can expire an outbox entry without notifying us. Retry
            // the same idempotent registration; only its new PUBACK advances.
            service->waitingForAck = false;
        }
        else
        {
            return;
        }
    }
    while (service->registrationIndex < registry->count &&
           !registry->configs[service->registrationIndex].enabled)
        ++service->registrationIndex;
    if (service->registrationIndex < registry->count)
    {
        if (service->retry.retryPending &&
            static_cast<uint32_t>(nowMs - service->retry.retryAtMs) >= 0x80000000UL) return;
        char payload[TELEMETRY_JSON_CAPACITY] = {};
        size_t length = 0U;
        if (serializeGatewayConnectJson(&registry->configs[service->registrationIndex], payload,
                sizeof(payload), &length) && publish(GATEWAY_CONNECT_TOPIC, payload, length, context))
        {
            service->waitingForAck = true;
            service->ackDeadlineMs = nowMs + REGISTRATION_ACK_TIMEOUT_MS;
            service->retry.retryPending = false;
        }
        else
        {
            service->retry.retryPending = true;
            service->retry.retryAtMs = nowMs + TELEMETRY_DELIVERY_RETRY_INTERVAL_MS;
        }
        return;
    }
    Publisher publisher{publish, context};
    serviceTelemetryDelivery(buffer, registry, publishTelemetry, &publisher, &service->retry, nowMs);
}
}
