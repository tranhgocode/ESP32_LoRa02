#pragma once

#include "app_config.h"

#if GATEWAY_V1_ENABLED && GATEWAY_UPLINK_ENABLED
#include "adapters/lora_radio.h"

namespace gateway
{
namespace app
{
namespace uplink_uart_mode
{
bool initialize(uint32_t nowMs);
void handleRadio(const adapter::LoRaRadioEvent &event, uint32_t nowMs);
} // namespace uplink_uart_mode
} // namespace app
} // namespace gateway
#endif
