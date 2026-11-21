#pragma once
#include <stdint.h>
// ESP32 pins connected to the LoRa-02 module. These values preserve the wiring
// used by the original gateway implementation.
constexpr uint8_t LORA_SCK = 18U;
constexpr uint8_t LORA_MISO = 19U;
constexpr uint8_t LORA_MOSI = 23U;
constexpr uint8_t LORA_NSS = 27U;
constexpr uint8_t LORA_RESET = 14U;
constexpr uint8_t LORA_DIO0 = 26U;

// Every sensor must use these same physical-layer parameters. Protocol V1
// changes frame contents only; it does not change the deployed radio settings.
constexpr long LORA_FREQUENCY = 433000000L;
constexpr uint8_t LORA_SYNC_WORD = 0x34U;
constexpr int LORA_SPREADING_FACTOR = 7;
constexpr long LORA_SIGNAL_BANDWIDTH = 125000L;
constexpr int LORA_CODING_RATE_DENOMINATOR = 5;
constexpr long LORA_PREAMBLE_SYMBOLS = 8L;

// Give the sensor time to switch from TX to RX before starting its ACK.
constexpr uint32_t LORA_ACK_TURNAROUND_MS = 20U;

// If the radio never reports TX_DONE, return to receive mode at the watchdog.
constexpr uint32_t LORA_TX_TIMEOUT_MS = 1000U;

#include "gateway_model.h"

#include <stddef.h>

namespace gateway_config
{
extern const gateway::SensorNodeConfig SENSOR_NODES[];
extern const size_t SENSOR_NODE_COUNT;
} // namespace gateway_config
