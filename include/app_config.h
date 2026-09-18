#pragma once

#include <stdint.h>

// Keep the binary path disabled by default until its timing is verified on a
// real LoRa-02 pair. PlatformIO can override this macro with
// -DGATEWAY_V1_ENABLED=1 for development and hardware acceptance.
#ifndef GATEWAY_V1_ENABLED
#define GATEWAY_V1_ENABLED 1
#endif

#if GATEWAY_V1_ENABLED != 0 && GATEWAY_V1_ENABLED != 1
#error "GATEWAY_V1_ENABLED must be 0 or 1"
#endif

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

// Async transmission should finish well inside this watchdog at the configured
// SF7/BW125 settings. Expiry reports a TX failure and restores receive mode so
// a missed DIO0 interrupt cannot leave the application stuck indefinitely.
constexpr uint32_t LORA_TX_TIMEOUT_MS = 1000U;

#if GATEWAY_V1_ENABLED
#include "gateway_model.h"

#include <stddef.h>

namespace gateway_config
{
/**
 * Build-time sensor list consumed by the V1 registry during setup.
 *
 * T08 starts with one enabled node. Later tasks may add entries here up to
 * gateway::MAX_SENSOR_NODES without changing the radio or coordinator APIs.
 */
extern const gateway::SensorNodeConfig SENSOR_NODES[];
extern const size_t SENSOR_NODE_COUNT;
} // namespace gateway_config
#endif
