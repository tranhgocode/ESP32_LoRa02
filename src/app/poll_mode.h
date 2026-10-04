#pragma once

#include "app_config.h"

#if GATEWAY_V1_ENABLED && !GATEWAY_UPLINK_ENABLED
#include "adapters/lora_radio.h"

namespace gateway
{
namespace app
{
namespace poll_mode
{
bool initialize(uint32_t nowMs);
void handleRadio(const adapter::LoRaRadioEvent &event, uint32_t nowMs);
void serviceTelemetry();
void serviceTick(uint32_t nowMs);
} // namespace poll_mode
} // namespace app
} // namespace gateway
#endif
