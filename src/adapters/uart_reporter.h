#pragma once

#include "app_config.h"

#include "uplink_coordinator.h"

namespace gateway
{
namespace adapter
{
/** Write one DATA or ERROR report if the entire line fits in the UART buffer. */
bool recordUplinkReport(const UplinkAction &report);
void reportAckResult(const PacketMessage &ack, bool sent, uint16_t batteryMv,
                     const SensorNodeConfig *node);
void reportRejectedFrame(size_t length);
void reportRadioStatus(uint8_t mode, uint32_t received, uint32_t dropped);
void reportMqttPayload(const char *topic, const char *payload, size_t payloadLength);
} // namespace adapter
} // namespace gateway
