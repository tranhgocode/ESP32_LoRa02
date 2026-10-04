#pragma once

#include <stddef.h>

// Initialize Wi-Fi and the ThingsBoard MQTT client.
void initMqtt();

// Maintain the Wi-Fi connection; call continuously from loop().
void handleMqtt();

// Enqueue one temperature and humidity sample with MQTT QoS 1.
bool sendTelemetry(float temperature, float humidity);

// Enqueue one serialized telemetry document with MQTT QoS 1.
// Return true only when the bounded client outbox accepts the message.
// The SDK copies payload bytes on acceptance. This does not mean broker PUBACK;
// the ESP-IDF mutex can wait, so V1 calls this only while the radio is idle.
bool sendTelemetryPayload(const char *payload, size_t payloadLength);
