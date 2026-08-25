#include "send_mqtt.h"

#include <Arduino.h>
#include <WiFi.h>
#include <mqtt_client.h>

#include "env_config.h"

namespace
{
  // MQTT client state and Wi-Fi retry timing are private to this module.
  esp_mqtt_client_handle_t mqttClient = nullptr;
  unsigned long lastWifiRetryTime = 0;
  constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;

  esp_err_t handleMqttEvent(esp_mqtt_event_handle_t event)
  {
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
      Serial.println("Status: MQTT connected to ThingsBoard");
      break;

    case MQTT_EVENT_DISCONNECTED:
      Serial.println("Warning: MQTT disconnected; waiting to reconnect...");
      break;

    case MQTT_EVENT_PUBLISHED:
      Serial.print("Status: ThingsBoard acknowledged QoS 1 message, ID: ");
      Serial.println(event->msg_id);
      break;

    case MQTT_EVENT_ERROR:
      Serial.println("Error: MQTT client event failed");
      break;

    default:
      break;
    }

    return ESP_OK;
  }

  void connectWifi()
  {
    Serial.print("Status: Connecting to Wi-Fi, SSID: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiRetryTime = millis();
  }
} // namespace

void initMqtt()
{
  // Use station mode because the ESP32 connects to an existing access point.
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectWifi();

  esp_mqtt_client_config_t mqttConfig = {};
  mqttConfig.host = MQTT_HOST;
  mqttConfig.port = MQTT_PORT;
  mqttConfig.username = THINGSBOARD_ACCESS_TOKEN;
  mqttConfig.password = nullptr;
  mqttConfig.event_handle = handleMqttEvent;
  mqttConfig.keepalive = 60;
  mqttConfig.reconnect_timeout_ms = 5000;

  mqttClient = esp_mqtt_client_init(&mqttConfig);
  if (mqttClient == nullptr)
  {
    Serial.println("Error: Failed to initialize MQTT client");
    return;
  }

  if (esp_mqtt_client_start(mqttClient) != ESP_OK)
  {
    Serial.println("Error: Failed to start MQTT client");
  }
}

void handleMqtt()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  const unsigned long now = millis();
  if (now - lastWifiRetryTime >= WIFI_RETRY_INTERVAL_MS)
  {
    // Retry at a fixed interval without blocking LoRa processing.
    connectWifi();
  }
}

bool sendTelemetry(float temperature, float humidity)
{
  if (mqttClient == nullptr)
  {
    Serial.println("Error: MQTT client is not ready");
    return false;
  }

  // Build the JSON payload in a fixed-size buffer to avoid dynamic allocation.
  char payload[96];
  const int payloadLength = snprintf(
      payload,
      sizeof(payload),
      "{\"temperature\":%.2f,\"humidity\":%.2f}",
      temperature,
      humidity);

  if (payloadLength < 0 || payloadLength >= static_cast<int>(sizeof(payload)))
  {
    Serial.println("Error: Telemetry payload exceeds the buffer size");
    return false;
  }

  // QoS 1, retain = false, store = true: keep the sample in the outbox if offline.
  const int messageId = esp_mqtt_client_enqueue(
      mqttClient,
      MQTT_TELEMETRY_TOPIC,
      payload,
      payloadLength,
      1,
      0,
      true);

  if (messageId < 0)
  {
    Serial.println("Error: Failed to enqueue MQTT telemetry");
    return false;
  }

  Serial.print("Data: MQTT telemetry queued [");
  Serial.print(MQTT_TELEMETRY_TOPIC);
  Serial.print("]: ");
  Serial.println(payload);
  return true;
}
