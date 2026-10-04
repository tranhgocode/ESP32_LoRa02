#pragma once

#include "app_config.h"

#if !GATEWAY_V1_ENABLED
#include "adapters/lora_radio.h"

namespace gateway
{
namespace app
{
namespace legacy_mode
{
void handleRadio(const adapter::LoRaRadioEvent &event);
} // namespace legacy_mode
} // namespace app
} // namespace gateway
#endif
