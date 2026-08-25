#pragma once

// ESP32 pins connected to the LoRa-02 module.
constexpr uint8_t LORA_SCK = 18;
constexpr uint8_t LORA_MISO = 19;
constexpr uint8_t LORA_MOSI = 23;
constexpr uint8_t LORA_NSS = 27;
constexpr uint8_t LORA_RESET = 14;
constexpr uint8_t LORA_DIO0 = 26;

// Radio settings must match on both the transmitter and receiver.
constexpr long LORA_FREQUENCY = 433E6;
constexpr uint8_t LORA_SYNC_WORD = 0x34;
constexpr unsigned long LORA_SEND_INTERVAL_MS = 2000;

// Build mode: 1 sends test packets; 0 receives telemetry packets.
#define LORA_MODE_TX 0
