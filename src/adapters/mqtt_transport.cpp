#include "mqtt_transport.h"
#include "mqtt_ack_queue.h"

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
// Queued MQTT packets live in RAM. This adapter enforces an 8 KiB soft ceiling
// before handing each packet to the SDK outbox.
constexpr size_t MQTT_OUTBOX_SOFT_LIMIT_BYTES = 8U * 1024U;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
constexpr unsigned long MQTT_STATUS_LOG_INTERVAL_MS = 10000UL;

esp_mqtt_client_handle_t mqttClient = nullptr;
// MQTT callback is the sole producer; loop is the sole consumer. If the
// bounded ACK ring overflows we re-register, never infer a missing PUBACK.
gateway::adapter::MqttAckQueue ackQueue;
std::atomic<uint32_t> connectionGeneration{0U};
gateway::adapter::RegistrationAckTracker registrationAck;
std::atomic<bool> mqttConnected{false};
std::atomic<uint32_t> mqttAcceptedCount{0U};
std::atomic<uint32_t> mqttPublishedCount{0U};
unsigned long lastWifiRetryTime = 0UL;
unsigned long lastMqttStatusLogTime = 0UL;
uint32_t lastLoggedAcceptedCount = 0U;
uint32_t lastLoggedPublishedCount = 0U;

/** Publish callbacks can run on the MQTT task, so readiness is atomic. */
void handleMqttEvent(void *, esp_event_base_t, int32_t, void *eventData)
{
  const auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
  if (event == nullptr)
  {
    return;
  }

  if (event->event_id == MQTT_EVENT_CONNECTED)
  {
    ++connectionGeneration;
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
    ackQueue.push(connectionGeneration.load(std::memory_order_acquire), event->msg_id);
    mqttPublishedCount.fetch_add(1U, std::memory_order_relaxed);
  }
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
  mqttConfig.broker.address.hostname = MQTT_HOST;
  mqttConfig.broker.address.port = MQTT_PORT;
  mqttConfig.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  mqttConfig.credentials.client_id = MQTT_CLIENT_ID;
  mqttConfig.credentials.username = MQTT_USERNAME;
  mqttConfig.credentials.authentication.password = MQTT_PASSWORD;
  mqttConfig.session.keepalive = 60;
  mqttConfig.network.reconnect_timeout_ms = 5000;
  mqttConfig.network.timeout_ms = 100;

  mqttClient = esp_mqtt_client_init(&mqttConfig);
  if (mqttClient == nullptr)
  {
    Serial.println("Error: Failed to initialize MQTT client");
    return;
  }

  if (esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY,
                                   handleMqttEvent, nullptr) != ESP_OK)
  {
    Serial.println("Error: Failed to register MQTT event handler");
    esp_mqtt_client_destroy(mqttClient);
    mqttClient = nullptr;
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

gateway::MqttSession mqttSession()
{
  gateway::MqttSession session{};
  gateway::adapter::PublishedAck ack{};
  while (ackQueue.pop(&ack))
  {
    registrationAck.observe(ack);
  }
  session.registrationLost = ackQueue.takeOverflow();
  if (session.registrationLost) registrationAck.invalidate();
  session.connected = mqttConnected.load(std::memory_order_acquire);
  session.generation = connectionGeneration;
  session.registrationAcknowledged = registrationAck.isAcknowledged();
  return session;
}

bool enqueueMqttPayload(const char *topic, const char *payload, size_t payloadLength, uint32_t generation)
{
  if (topic == nullptr || generation != connectionGeneration) return false;
  if (mqttClient == nullptr ||
      !mqttConnected.load(std::memory_order_acquire) ||
      payload == nullptr || payloadLength == 0U ||
      payloadLength > static_cast<size_t>(INT_MAX))
  {
    return false;
  }

  const size_t topicLength = strlen(topic);
  size_t packetLength = 0U;
  if (!mqttQos1PacketSize(topicLength, payloadLength, &packetLength) ||
      packetLength > MQTT_OUTBOX_SOFT_LIMIT_BYTES)
  {
    return false;
  }

  // Check current outbox bytes against the application's soft ceiling.
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
      topic,
      payload,
      static_cast<int>(payloadLength),
      1,
      0,
      true);
  if (messageId < 0)
  {
    return false;
  }

  if (strcmp(topic, gateway::GATEWAY_CONNECT_TOPIC) == 0)
  {
    registrationAck.expect(generation, messageId);
  }
  // A successful SDK handoff already owns the payload, even if reconnect raced
  // enqueue. Report acceptance so the application cannot submit the same RAM
  // entry again. The next observed session resets device registration.
  mqttAcceptedCount.fetch_add(1U, std::memory_order_relaxed);
  return true;
}
