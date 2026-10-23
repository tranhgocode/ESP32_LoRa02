#pragma once
#include "adapters/lora_radio.h"
namespace gateway { namespace app { namespace gateway_app {
bool initialize();
void handleRadio(const adapter::LoRaRadioEvent &event, uint32_t nowMs);
void serviceTelemetry(uint32_t nowMs);
} } }
