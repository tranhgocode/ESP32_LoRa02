#include "mqtt_transport.h"

#include <Arduino.h>
#include <WiFi.h>
#include <mqtt_client.h>

#include <atomic>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "env_config.h"

namespace
{
// ESP-IDF 4.4.7 stores queued MQTT packets in RAM. ESP-MQTT exposes no hard
// outbox limit in this SDK, so this adapter enforces an 8 KiB soft ceiling.
constexpr size_t MQTT_OUTBOX_SOFT_LIMIT_BYTES = 8U * 1024U;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
constexpr unsigned long MQTT_STATUS_LOG_INTERVAL_MS = 10000UL;

esp_mqtt_client_handle_t mqttClient = nullptr;
std::atomic<bool> mqttConnected{false};
std::atomic<uint32_t> mqttAcceptedCount{0U};
std::atomic<uint32_t> mqttPublishedCount{0U};
unsigned long lastWifiRetryTime = 0UL;
unsigned long lastMqttStatusLogTime = 0UL;
uint32_t lastLoggedAcceptedCount = 0U;
uint32_t lastLoggedPublishedCount = 0U;

/** Publish callbacks can run on the MQTT task, so readiness is atomic. */
esp_err_t handleMqttEvent(esp_mqtt_event_handle_t event)
{
  if (event == nullptr)
  {
    return ESP_OK;
  }

  if (event->event_id == MQTT_EVENT_CONNECTED)
  {
    if (!mqttConnected.exchange(true, std::memory_order_acq_rel))
    {
      Serial.println("Status: MQTT connected to ThingsBoard");
    }
  }
  else if (event->event_id == MQTT_EVENT_DISCONNECTED)
  {
    if (mqttConnected.exchange(false, std::memory_order_acq_rel))
    {
      Serial.println("Warning: MQTT disconnected; waiting to reconnect");
    }
  }
  else if (event->event_id == MQTT_EVENT_PUBLISHED)
  {
    mqttPublishedCount.fetch_add(1U, std::memory_order_relaxed);
  }

  return ESP_OK;
}

void connectWifi()
{
  // SSID and password are configuration secrets; keep them out of serial logs.
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiRetryTime = millis();
}

/** Count the fixed header, remaining-length bytes, topic, id, and payload. */
bool mqttQos1PacketSize(size_t topicLength,
                       size_t payloadLength,
                       size_t *packetLength)
{
  if (packetLength == nullptr || topicLength > 0xFFFFU ||
      payloadLength > SIZE_MAX - topicLength - 6U)
  {
    return false;
  }

  const size_t remainingLength = topicLength + payloadLength + 4U;
  size_t encodedLengthBytes = 1U;
  if (remainingLength >= 128U)
  {
    encodedLengthBytes = 2U;
    if (remainingLength >= 16384U)
    {
      encodedLengthBytes = 3U;
      if (remainingLength >= 2097152U)
      {
        encodedLengthBytes = 4U;
      }
    }
  }

  *packetLength = 1U + encodedLengthBytes + remainingLength;
  return true;
}
} // namespace

void initMqtt()
{
  mqttConnected.store(false, std::memory_order_release);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectWifi();

  esp_mqtt_client_config_t mqttConfig = {};
  mqttConfig.host = MQTT_HOST;
  mqttConfig.port = MQTT_PORT;
  mqttConfig.client_id = MQTT_CLIENT_ID;
  mqttConfig.username = MQTT_USERNAME;
  mqttConfig.password = MQTT_PASSWORD;
  mqttConfig.event_handle = handleMqttEvent;
  mqttConfig.keepalive = 60;
  mqttConfig.reconnect_timeout_ms = 5000;
  mqttConfig.network_timeout_ms = 100;

  mqttClient = esp_mqtt_client_init(&mqttConfig);
  if (mqttClient == nullptr)
  {
    Serial.println("Error: Failed to initialize MQTT client");
    return;
  }

  if (esp_mqtt_client_start(mqttClient) != ESP_OK)
  {
    mqttConnected.store(false, std::memory_order_release);
    Serial.println("Error: Failed to start MQTT client");
  }
}

void handleMqtt()
{
  const unsigned long now = millis();
  if (now - lastMqttStatusLogTime >= MQTT_STATUS_LOG_INTERVAL_MS)
  {
    const uint32_t accepted =
        mqttAcceptedCount.load(std::memory_order_relaxed);
    const uint32_t published =
        mqttPublishedCount.load(std::memory_order_relaxed);
    if (accepted != lastLoggedAcceptedCount ||
        published != lastLoggedPublishedCount)
    {
      Serial.print("Status: MQTT handoff totals, enqueued: ");
      Serial.print(accepted);
      Serial.print(" | QoS 1 PUBACK: ");
      Serial.println(published);
      lastLoggedAcceptedCount = accepted;
      lastLoggedPublishedCount = published;
    }
    lastMqttStatusLogTime = now;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  if (now - lastWifiRetryTime >= WIFI_RETRY_INTERVAL_MS)
  {
    connectWifi();
  }
}

bool sendTelemetry(float temperature, float humidity)
{
  char payload[96] = {};
  const int payloadLength = snprintf(
      payload,
      sizeof(payload),
      "{\"T\":%.2f,\"H\":%.2f}",
      temperature,
      humidity);

  if (payloadLength < 0 || payloadLength >= static_cast<int>(sizeof(payload)))
  {
    return false;
  }

  return sendTelemetryPayload(payload, static_cast<size_t>(payloadLength));
}

bool sendTelemetryPayload(const char *payload, size_t payloadLength)
{
  if (mqttClient == nullptr ||
      !mqttConnected.load(std::memory_order_acquire) ||
      payload == nullptr || payloadLength == 0U ||
      payloadLength > static_cast<size_t>(INT_MAX))
  {
    return false;
  }

  const size_t topicLength = strlen(MQTT_TELEMETRY_TOPIC);
  size_t packetLength = 0U;
  if (!mqttQos1PacketSize(topicLength, payloadLength, &packetLength) ||
      packetLength > MQTT_OUTBOX_SOFT_LIMIT_BYTES)
  {
    return false;
  }

  // This SDK reports the current outbox bytes but does not impose a hard cap.
  // One application loop is the sole producer, so this check bounds our writes.
  const int currentOutboxBytes = esp_mqtt_client_get_outbox_size(mqttClient);
  if (currentOutboxBytes < 0 ||
      static_cast<size_t>(currentOutboxBytes) >
          MQTT_OUTBOX_SOFT_LIMIT_BYTES - packetLength ||
      !mqttConnected.load(std::memory_order_acquire))
  {
    return false;
  }

  const int messageId = esp_mqtt_client_enqueue(
      mqttClient,
      MQTT_TELEMETRY_TOPIC,
      payload,
      static_cast<int>(payloadLength),
      1,
      0,
      true);
  if (messageId < 0)
  {
    return false;
  }

  mqttAcceptedCount.fetch_add(1U, std::memory_order_relaxed);
  return true;
}
