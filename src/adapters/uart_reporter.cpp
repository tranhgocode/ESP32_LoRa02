#include "app_config.h"

#include "uart_reporter.h"
#include <Arduino.h>
#include <stdio.h>

namespace gateway
{
namespace adapter
{
/** One bounded UART write, admitted only when the entire line fits now.
 * A short/failed write rejects recording, so it never authorizes an ACK.
 * UART acceptance confirms gateway processing, not durable storage.
 */
bool recordUplinkReport(const gateway::UplinkAction &report)
{
    char line[128] = {};
    int length = 0;
    if (report.hasSample)
    {
        length = snprintf(line, sizeof(line),
                          "DATA Src=%u Dest=%u ID=%u Seq=%u RSSI=%d SNRx4=%d temperature_x10=%d humidity_x10=%u\n",
                          report.packet.source, report.packet.destination,
                          report.packet.transactionId, report.packet.sequence,
                          report.rssi, report.snrX4,
                          report.sample.temperatureX10, report.sample.humidityX10);
    }
    else
    {
        length = snprintf(line, sizeof(line),
                          "ERROR Src=%u Dest=%u ID=%u Seq=%u RSSI=%d SNRx4=%d error_code=%u\n",
                          report.packet.source, report.packet.destination,
                          report.packet.transactionId, report.packet.sequence,
                          report.rssi, report.snrX4,
                          static_cast<uint8_t>(report.errorCode));
    }
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(line) ||
        Serial.availableForWrite() < length)
    {
        return false;
    }
    return Serial.write(reinterpret_cast<const uint8_t *>(line), length) ==
           static_cast<size_t>(length);
}
} // namespace adapter
} // namespace gateway
