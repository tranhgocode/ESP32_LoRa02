#include "my_lora.h"

#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>

#include "config.h"

namespace
{
uint32_t packetCounter = 0;
unsigned long lastSendTime = 0;

void sendPacket()
{
  ++packetCounter;

  LoRa.beginPacket();
  LoRa.print("Xin chao LoRa! Goi so ");
  LoRa.print(packetCounter);
  LoRa.endPacket();

  Serial.print("Da gui: Xin chao LoRa! Goi so ");
  Serial.println(packetCounter);
}

void receivePacket()
{
  const int packetSize = LoRa.parsePacket();
  if (packetSize == 0)
  {
    return;
  }

  Serial.print("Da nhan: ");
  while (LoRa.available())
  {
    Serial.write(LoRa.read());
  }

  Serial.print(" | RSSI: ");
  Serial.print(LoRa.packetRssi());
  Serial.print(" dBm | SNR: ");
  Serial.println(LoRa.packetSnr());
}
} // namespace

void initLoRa()
{
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);

  Serial.print("Khoi dong LoRa o ");
  Serial.print(LORA_FREQUENCY / 1000000UL);
  Serial.println(" MHz...");

  if (!LoRa.begin(LORA_FREQUENCY))
  {
    Serial.println("Khong tim thay LoRa-02");
    while (true)
    {
      delay(1000);
    }
  }

  Serial.println("LoRa da san sang");

#if LORA_MODE_TX
  Serial.println("Che do: PHAT");
  sendPacket();
  lastSendTime = millis();
#else
  Serial.println("Che do: NHAN");
#endif
}

void handleLoRa()
{
#if LORA_MODE_TX
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
