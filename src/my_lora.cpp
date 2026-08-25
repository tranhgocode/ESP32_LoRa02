#include "my_lora.h"

#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>
#include <math.h>
#include <stdlib.h>

#include "config.h"
#include "send_mqtt.h"

namespace
{
// State used only by the LoRa transmit mode.
uint32_t packetCounter = 0;
unsigned long lastSendTime = 0;

// Find a key in the payload and parse the floating-point value after it.
bool readNumberAfterKey(const String &payload, const char *key, float &value)
{
  const int keyPosition = payload.indexOf(key);
  if (keyPosition < 0)
  {
    return false;
  }

  const char *numberStart = payload.c_str() + keyPosition + strlen(key);
  char *numberEnd = nullptr;
  value = strtof(numberStart, &numberEnd);
  return numberEnd != numberStart && isfinite(value);
}

bool parseTelemetry(String payload, float &temperature, float &humidity)
{
  // Normalize the message so several common telemetry formats are accepted.
  payload.trim();
  payload.toUpperCase();
  payload.replace("\"", "");
  payload.replace(" ", "");

  const bool hasTemperature =
      readNumberAfterKey(payload, "TEMPERATURE:", temperature) ||
      readNumberAfterKey(payload, "TEMPERATURE=", temperature) ||
      readNumberAfterKey(payload, "T:", temperature) ||
      readNumberAfterKey(payload, "T=", temperature);

  const bool hasHumidity =
      readNumberAfterKey(payload, "HUMIDITY:", humidity) ||
      readNumberAfterKey(payload, "HUMIDITY=", humidity) ||
      readNumberAfterKey(payload, "H:", humidity) ||
      readNumberAfterKey(payload, "H=", humidity);

  return hasTemperature && hasHumidity;
}

void sendPacket()
{
  ++packetCounter;

  // Send a simple test message so the transmitter can be verified independently.
  LoRa.beginPacket();
  LoRa.print("Hello LoRa! Packet number ");
  LoRa.print(packetCounter);
  LoRa.endPacket();

  Serial.print("Data: Sent LoRa packet: Hello LoRa! Packet number ");
  Serial.println(packetCounter);
}

void receivePacket()
{
  const int packetSize = LoRa.parsePacket();
  if (packetSize == 0)
  {
    return;
  }

  // Copy the complete received packet into a String before parsing it.
  String payload;
  payload.reserve(packetSize);
  while (LoRa.available())
  {
    payload += static_cast<char>(LoRa.read());
  }

  Serial.print("Data: Received LoRa packet: ");
  Serial.print(payload);
  Serial.print(" | RSSI: ");
  Serial.print(LoRa.packetRssi());
  Serial.print(" dBm | SNR: ");
  Serial.println(LoRa.packetSnr());

  float temperature = 0.0F;
  float humidity = 0.0F;
  if (!parseTelemetry(payload, temperature, humidity))
  {
    Serial.println("Error: Invalid telemetry format. Example: T:25.5,H:60.2");
    return;
  }

  // Forward valid sensor data to ThingsBoard through MQTT.
  sendTelemetry(temperature, humidity);
}
} // namespace

void initLoRa()
{
  // Configure the ESP32 SPI bus and the control pins connected to the LoRa module.
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);

  Serial.print("Status: Starting LoRa at ");
  Serial.print(LORA_FREQUENCY / 1000000UL);
  Serial.println(" MHz...");

  if (!LoRa.begin(LORA_FREQUENCY))
  {
    Serial.println("Error: LoRa-02 module was not detected");
    // Stop here because the application cannot use LoRa without the radio.
    while (true)
    {
      delay(1000);
    }
  }

  // Both LoRa nodes must use the same radio parameters to communicate.
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.setSyncWord(LORA_SYNC_WORD);

  Serial.println("Status: LoRa is ready");

#if LORA_MODE_TX
  Serial.println("Mode: LoRa transmitter");
  sendPacket();
  lastSendTime = millis();
#else
  Serial.println("Mode: LoRa receiver");
#endif
}

void handleLoRa()
{
#if LORA_MODE_TX
  // The subtraction remains safe when millis() wraps around.
  const unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= LORA_SEND_INTERVAL_MS)
  {
    lastSendTime = currentTime;
    sendPacket();
  }
#else
  receivePacket();
#endif
}
