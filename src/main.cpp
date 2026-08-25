#include <Arduino.h>
#include "my_lora.h"
#include "send_mqtt.h"

void setup()
{
  // Start diagnostics before initializing network and radio services.
  Serial.begin(115200);
  delay(1000);

  // MQTT is initialized first so received LoRa telemetry can be forwarded.
  initMqtt();
  initLoRa();
}

void loop()
{
  // Keep both services responsive without blocking delays.
  handleMqtt();
  handleLoRa();
}
