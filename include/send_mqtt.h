#pragma once

// Initialize Wi-Fi and the ThingsBoard MQTT client.
void initMqtt();

// Maintain the Wi-Fi connection; call continuously from loop().
void handleMqtt();

// Enqueue one temperature and humidity sample with MQTT QoS 1.
// Return true when the sample is successfully added to the MQTT queue.
bool sendTelemetry(float temperature, float humidity);
