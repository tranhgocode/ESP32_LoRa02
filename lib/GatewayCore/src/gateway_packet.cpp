#include "gateway_packet.h"

#include <string.h>

namespace gateway
{
namespace
{
// Offsets for the fixed five-byte header. Sequence and CRC follow the variable
// payload, so their offsets are calculated for each packet.
constexpr size_t TYPE_OFFSET = 0U;
constexpr size_t SOURCE_OFFSET = 1U;
constexpr size_t DESTINATION_OFFSET = 2U;
constexpr size_t TRANSACTION_ID_OFFSET = 3U;
constexpr size_t PAYLOAD_LENGTH_OFFSET = 4U;
constexpr size_t PAYLOAD_OFFSET = 5U;
constexpr size_t SEQUENCE_SIZE = 2U;

constexpr size_t TEMPERATURE_OFFSET = 0U;
constexpr size_t HUMIDITY_OFFSET = 2U;
constexpr size_t BATTERY_OFFSET = DATA_PAYLOAD_LENGTH;
constexpr size_t PM25_OFFSET = DATA_BATTERY_PAYLOAD_LENGTH;
constexpr size_t FORECAST_OFFSET = DATA_PM25_PAYLOAD_LENGTH;
static_assert(FORECAST_OFFSET + sizeof(uint16_t) * PM25_FORECAST_COUNT ==
                  DATA_FORECAST_PAYLOAD_LENGTH,
              "The DATA forecast must contain four 16-bit values");

constexpr uint16_t CRC_INITIAL_VALUE = 0xFFFFU;
constexpr uint16_t CRC_POLYNOMIAL = 0x1021U;
constexpr uint16_t CRC_TOP_BIT = 0x8000U;

/** Return true only for addresses assignable to a physical sensor node. */
bool isNodeAddress(uint8_t address)
{
    return address >= MIN_NODE_ADDRESS && address <= MAX_NODE_ADDRESS;
}

/**
 * Restrict ERROR payloads to values understood by the V1 sensor and gateway.
 * Checking the numeric range is safe because all four defined values are
 * contiguous in the protocol.
 */
bool isKnownErrorCode(uint8_t value)
{
    return value >= static_cast<uint8_t>(PacketErrorCode::DhtInitFailed) &&
           value <= static_cast<uint8_t>(PacketErrorCode::PacketBuildFailed);
}

bool isDataLengthSupported(uint8_t length)
{
    return length == DATA_PAYLOAD_LENGTH ||
           length == DATA_BATTERY_PAYLOAD_LENGTH ||
           length == DATA_PM25_PAYLOAD_LENGTH ||
           length == DATA_FORECAST_PAYLOAD_LENGTH;
}

uint16_t readUint16(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) << 8U |
           static_cast<uint16_t>(bytes[1]);
}

void writeUint16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value >> 8U);
    bytes[1] = static_cast<uint8_t>(value);
}

/**
 * Validate the direction and payload contract associated with each type.
 * This function is shared by encoding and decoding so locally-created packets
 * are held to the same rules as received radio frames.
 */
bool isMessageValid(const PacketMessage &message)
{
    switch (message.type)
    {
    case PacketType::Poll:
        return message.source == GATEWAY_ADDRESS &&
               isNodeAddress(message.destination) &&
               message.payloadLength == 0U &&
               message.sequence == 0U;

    case PacketType::Data:
        return isNodeAddress(message.source) &&
               message.destination == GATEWAY_ADDRESS &&
               isDataLengthSupported(message.payloadLength);

    case PacketType::Ack:
        return message.source == GATEWAY_ADDRESS &&
               isNodeAddress(message.destination) &&
               message.payloadLength == 0U;

    case PacketType::Error:
        return isNodeAddress(message.source) &&
               message.destination == GATEWAY_ADDRESS &&
               message.payloadLength == ERROR_PAYLOAD_LENGTH &&
               isKnownErrorCode(message.payload[0]);

    default:
        return false;
    }
}

/**
 * Calculate CRC-16/CCITT-FALSE without allocating memory or using hardware.
 * Each input byte is placed in the high CRC byte, then processed most
 * significant bit first with polynomial 0x1021.
 */
uint16_t calculateCrc(const uint8_t *data, size_t length)
{
    uint16_t crc = CRC_INITIAL_VALUE;

    for (size_t byteIndex = 0U; byteIndex < length; ++byteIndex)
    {
        crc ^= static_cast<uint16_t>(data[byteIndex]) << 8U;

        for (uint8_t bitIndex = 0U; bitIndex < 8U; ++bitIndex)
        {
            if ((crc & CRC_TOP_BIT) != 0U)
            {
                crc = static_cast<uint16_t>((crc << 1U) ^ CRC_POLYNOMIAL);
            }
            else
            {
                crc = static_cast<uint16_t>(crc << 1U);
            }
        }
    }

    return crc;
}

void writeFrame(const PacketMessage &message, uint8_t *frame)
{
    frame[TYPE_OFFSET] = static_cast<uint8_t>(message.type);
    frame[SOURCE_OFFSET] = message.source;
    frame[DESTINATION_OFFSET] = message.destination;
    frame[TRANSACTION_ID_OFFSET] = message.transactionId;
    frame[PAYLOAD_LENGTH_OFFSET] = message.payloadLength;
    memcpy(&frame[PAYLOAD_OFFSET], message.payload, message.payloadLength);

    const size_t sequenceOffset = PAYLOAD_OFFSET + message.payloadLength;
    const size_t crcOffset = sequenceOffset + SEQUENCE_SIZE;
    writeUint16(&frame[sequenceOffset], message.sequence);
    writeUint16(&frame[crcOffset], calculateCrc(frame, crcOffset));
}

PacketMessage readFrame(const uint8_t *input, uint8_t payloadLength)
{
    PacketMessage message{};
    message.type = static_cast<PacketType>(input[TYPE_OFFSET]);
    message.source = input[SOURCE_OFFSET];
    message.destination = input[DESTINATION_OFFSET];
    message.transactionId = input[TRANSACTION_ID_OFFSET];
    message.payloadLength = payloadLength;
    memcpy(message.payload, &input[PAYLOAD_OFFSET], payloadLength);
    message.sequence = readUint16(&input[PAYLOAD_OFFSET + payloadLength]);
    return message;
}

int16_t readTemperature(const uint8_t *payload)
{
    const uint16_t bits = readUint16(&payload[TEMPERATURE_OFFSET]);
    int32_t temperature = bits;
    // Apply the two's-complement wrap before narrowing to int16_t.
    if ((bits & 0x8000U) != 0U)
    {
        temperature -= 0x10000L;
    }
    return static_cast<int16_t>(temperature);
}

OptionalSensorData readOptionalData(const PacketMessage &message)
{
    OptionalSensorData data{};
    // Each supported length extends the previous layout by whole fields.
    if (message.payloadLength >= DATA_BATTERY_PAYLOAD_LENGTH)
    {
        data.batteryMv = readUint16(&message.payload[BATTERY_OFFSET]);
    }
    if (message.payloadLength >= DATA_PM25_PAYLOAD_LENGTH)
    {
        data.pm25UgM3 = readUint16(&message.payload[PM25_OFFSET]);
    }
    if (message.payloadLength == DATA_FORECAST_PAYLOAD_LENGTH)
    {
        for (size_t step = 0U; step < PM25_FORECAST_COUNT; ++step)
        {
            const size_t offset = FORECAST_OFFSET + step * sizeof(uint16_t);
            data.pm25Forecast[step] = readUint16(&message.payload[offset]);
        }
    }
    return data;
}
} // namespace

bool encodePacket(const PacketMessage *message,
                  uint8_t *output,
                  size_t outputCapacity,
                  size_t *outputLength)
{
    if (message == nullptr || output == nullptr || outputLength == nullptr ||
        !isMessageValid(*message))
    {
        return false;
    }

    const size_t frameLength = MIN_FRAME_LENGTH + message->payloadLength;
    if (outputCapacity < frameLength)
    {
        return false;
    }

    // Build privately before copying a complete frame to the caller.
    uint8_t frame[MAX_FRAME_LENGTH] = {};
    writeFrame(*message, frame);
    memcpy(output, frame, frameLength);
    *outputLength = frameLength;
    return true;
}

bool decodePacket(const uint8_t *input,
                  size_t inputLength,
                  PacketMessage *message)
{
    // The length guard makes every later access to the five-byte header, the
    // sequence, and the CRC safe.
    if (input == nullptr || message == nullptr ||
        inputLength < MIN_FRAME_LENGTH || inputLength > MAX_FRAME_LENGTH)
    {
        return false;
    }

    const uint8_t payloadLength = input[PAYLOAD_LENGTH_OFFSET];
    if (payloadLength > MAX_PAYLOAD_LENGTH ||
        inputLength != MIN_FRAME_LENGTH + payloadLength)
    {
        return false;
    }

    const size_t crcOffset = PAYLOAD_OFFSET + payloadLength + SEQUENCE_SIZE;

    // CRC is checked before the received bytes can influence application state.
    if (readUint16(&input[crcOffset]) != calculateCrc(input, crcOffset))
    {
        return false;
    }

    const PacketMessage decoded = readFrame(input, payloadLength);
    if (!isMessageValid(decoded))
    {
        return false;
    }

    // Assign last so a rejected frame cannot partially overwrite caller state.
    *message = decoded;
    return true;
}

bool decodeDataPayload(const PacketMessage *message,
                       int16_t *temperatureX10,
                       uint16_t *humidityX10)
{
    SensorData data{};
    if (temperatureX10 == nullptr || humidityX10 == nullptr ||
        !decodeDataPayload(message, &data))
    {
        return false;
    }

    *temperatureX10 = data.temperatureX10;
    *humidityX10 = data.humidityX10;
    return true;
}

bool decodeDataPayload(const PacketMessage *message, SensorData *data)
{
    if (message == nullptr || data == nullptr || message->type != PacketType::Data ||
        !isDataLengthSupported(message->payloadLength))
    {
        return false;
    }

    data->temperatureX10 = readTemperature(message->payload);
    data->humidityX10 = readUint16(&message->payload[HUMIDITY_OFFSET]);
    data->optional = readOptionalData(*message);
    return true;
}

bool decodeErrorPayload(const PacketMessage *message,
                        PacketErrorCode *errorCode)
{
    if (message == nullptr || errorCode == nullptr)
    {
        return false;
    }

    if (message->type != PacketType::Error ||
        message->payloadLength != ERROR_PAYLOAD_LENGTH ||
        !isKnownErrorCode(message->payload[0]))
    {
        return false;
    }

    *errorCode = static_cast<PacketErrorCode>(message->payload[0]);
    return true;
}
} // namespace gateway
