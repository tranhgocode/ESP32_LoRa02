#pragma once

#include "app_config.h"

#if GATEWAY_V1_ENABLED && GATEWAY_UPLINK_ENABLED
#include "uplink_coordinator.h"

namespace gateway
{
namespace adapter
{
/** Record one complete bounded UART report; false never authorizes ACK. */
bool recordUplinkReport(const UplinkAction &report);
} // namespace adapter
} // namespace gateway
#endif
