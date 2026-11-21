#pragma once

#include "gateway_model.h"

namespace gateway
{
/** Set a rounded charge percentage when the reading and curve are valid. */
bool calculateBatteryLevel(uint16_t millivolts, const BatteryCurve &curve, uint8_t &percent);
} // namespace gateway
