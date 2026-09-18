#pragma once

#include <stdint.h>

namespace gateway
{
/**
 * Complete in-memory reading produced from one accepted DATA response.
 *
 * Values stay in the same integer fixed-point representation used by the LoRa
 * protocol. Keeping conversion out of this type avoids floating-point work in
 * the radio path and lets the later MQTT serializer decide how to format each
 * value. The node and sequence fields preserve the source identity needed for
 * delivery and diagnostics.
 */
struct TelemetrySample
{
    uint8_t nodeId = 0U;
    uint16_t sequence = 0U;
    int16_t temperatureX10 = 0;
    uint16_t humidityX10 = 0U;
    int16_t rssi = 0;
    int8_t snrX4 = 0;
    uint32_t receivedAtMs = 0U;
};
} // namespace gateway
