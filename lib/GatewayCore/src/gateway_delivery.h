#pragma once
#include "telemetry_delivery.h"
namespace gateway
{
constexpr const char *GATEWAY_CONNECT_TOPIC = "v1/gateway/connect";
constexpr const char *GATEWAY_TELEMETRY_TOPIC = "v1/gateway/telemetry";
struct MqttSession
{
    bool connected = false;
    uint32_t generation = 0U;
    bool registrationLost = false;
    bool registrationAcknowledged = false;
};
constexpr uint32_t REGISTRATION_ACK_TIMEOUT_MS = 10000U;
struct GatewayDelivery
{
    uint32_t generation = 0U;
    size_t registrationIndex = 0U;
    bool waitingForAck = false;
    uint32_t ackDeadlineMs = 0U;
    TelemetryDeliveryService retry{};
};
using GatewayPublishFunction = bool (*)(const char *topic, const char *payload, size_t length, void *context);
// At most one publisher call. Connect PUBACK is required before advancing.
void serviceGatewayDelivery(GatewayDelivery *service, const SensorRegistry *registry,
    TelemetryBuffer *buffer, const MqttSession &session, GatewayPublishFunction publish,
    void *context, uint32_t nowMs);
}
