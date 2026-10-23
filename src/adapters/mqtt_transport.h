#pragma once
#include "gateway_delivery.h"
void initMqtt();
void handleMqtt();
gateway::MqttSession mqttSession();
// Copies payload into bounded QoS 1 outbox. Acceptance is not broker delivery.
bool enqueueMqttPayload(const char *topic, const char *payload, size_t length, uint32_t generation);
