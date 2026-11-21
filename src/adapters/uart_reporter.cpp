#include "app_config.h"

#include "uart_reporter.h"
#include "battery_level.h"
#include "telemetry_json.h"
#include <Arduino.h>
#include <stdio.h>

namespace gateway
{
namespace adapter
{
namespace
{
bool writeReportLine(const char *line, size_t capacity, int length)
{
    if (length <= 0 || static_cast<size_t>(length) >= capacity ||
        Serial.availableForWrite() < length)
    {
        return false;
    }

    return Serial.write(reinterpret_cast<const uint8_t *>(line), length) ==
           static_cast<size_t>(length);
}
} // namespace

/** Write one complete line when the UART has room for it. */
bool recordUplinkReport(const gateway::UplinkAction &report)
{
    char line[128] = {};
    int length = 0;
    if (report.hasSample)
    {
        length = snprintf(line, sizeof(line),
                          "LoRa RX DATA Src=%u Dest=%u ID=%u Seq=%u RSSI=%d SNRx4=%d "
                          "temperature_x10=%d humidity_x10=%u\n",
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
    return writeReportLine(line, sizeof(line), length);
}

void reportAckResult(const PacketMessage &ack, bool sent, uint16_t batteryMv,
                     const SensorNodeConfig *node)
{
    // Use the same conversion as MQTT and keep the ACK report in one UART write.
    char battery[40] = {};
    if (batteryMv != SENSOR_VALUE_UNAVAILABLE)
    {
        uint8_t level = 0U;
        const unsigned int millivolts = batteryMv;
        if (node != nullptr && calculateBatteryLevel(batteryMv, node->batteryCurve, level))
        {
            snprintf(battery, sizeof(battery), " battery_mv=%u batteryLevel=%u%%",
                     millivolts, static_cast<unsigned int>(level));
        }
        else
        {
            snprintf(battery, sizeof(battery), " battery_mv=%u", millivolts);
        }
    }

    char line[96] = {};
    const int length = snprintf(line, sizeof(line), "LoRa ACK %s node=%u ID=%u Seq=%u%s\n",
                                sent ? "sent" : "failed",
                                static_cast<unsigned int>(ack.destination),
                                static_cast<unsigned int>(ack.transactionId),
                                static_cast<unsigned int>(ack.sequence),
                                battery);
    writeReportLine(line, sizeof(line), length);
}

void reportRejectedFrame(size_t frameLength)
{
    char line[64] = {};
    const int length = snprintf(line, sizeof(line), "Warning: LoRa frame rejected, bytes=%u\n",
                                static_cast<unsigned int>(frameLength));
    writeReportLine(line, sizeof(line), length);
}

void reportRadioStatus(uint8_t mode, uint32_t received, uint32_t dropped)
{
    char line[96] = {};
    const int length = snprintf(line, sizeof(line),
                                "Status: LoRa mode=0x%02X rx_frames=%lu radio_dropped=%lu\n",
                                static_cast<unsigned int>(mode),
                                static_cast<unsigned long>(received),
                                static_cast<unsigned long>(dropped));
    writeReportLine(line, sizeof(line), length);
}

void reportMqttPayload(const char *topic, const char *payload, size_t payloadLength)
{
    if (payloadLength >= TELEMETRY_JSON_CAPACITY)
    {
        return;
    }

    // Print the same JSON bytes that went into the MQTT outbox.
    char line[TELEMETRY_JSON_CAPACITY + 64U] = {};
    const int length = snprintf(line, sizeof(line), "MQTT queued topic=%s payload=%.*s\n",
                                topic, static_cast<int>(payloadLength), payload);
    writeReportLine(line, sizeof(line), length);
}
} // namespace adapter
} // namespace gateway
