#pragma once

// Cac tham so cau hinh cho LoRa-02
constexpr uint8_t LORA_SCK = 18;
constexpr uint8_t LORA_MISO = 19;
constexpr uint8_t LORA_MOSI = 23;
constexpr uint8_t LORA_NSS = 27;
constexpr uint8_t LORA_RESET = 14;
constexpr uint8_t LORA_DIO0 = 26;

// LoRa-02 thuong dung 433 MHz
constexpr long LORA_FREQUENCY = 433E6;
constexpr unsigned long LORA_SEND_INTERVAL_MS = 2000;

// 1: phat goi tin; 0: nhan goi tin
#define LORA_MODE_TX 1
