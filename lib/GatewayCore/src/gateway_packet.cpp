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

/**
 * Validate the direction and fixed payload contract associated with each type.
 * This function is shared by encoding and decoding so locally-created packets
 * are held to the same rules as received radio frames.
 */
bool isMessageValid(const PacketMessage &message)
{
    if (message.payloadLength > MAX_PAYLOAD_LENGTH)
    {
        return false;
    }

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
               message.payloadLength == DATA_PAYLOAD_LENGTH;

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
} // namespace

bool encodePacket(const PacketMessage *message,
                  uint8_t *output,
                  size_t outputCapacity,
                  size_t *outputLength)
{
    // Validate pointers before dereferencing them. Requiring all three outputs
    // up front gives the function a simple all-or-nothing result contract.
    if (message == nullptr || output == nullptr || outputLength == nullptr)
    {
        return false;
    }

    if (!isMessageValid(*message))
    {
        return false;
    }

    const size_t frameLength = MIN_FRAME_LENGTH + message->payloadLength;
    if (outputCapacity < frameLength)
    {
        return false;
    }

    // The temporary frame preserves the caller's output when any validation or
    // size check fails. Its fixed 64-byte storage is suitable for the ESP32 stack.
    uint8_t frame[MAX_FRAME_LENGTH] = {};
    frame[TYPE_OFFSET] = static_cast<uint8_t>(message->type);
    frame[SOURCE_OFFSET] = message->source;
    frame[DESTINATION_OFFSET] = message->destination;
    frame[TRANSACTION_ID_OFFSET] = message->transactionId;
    frame[PAYLOAD_LENGTH_OFFSET] = message->payloadLength;

    if (message->payloadLength > 0U)
    {
        memcpy(&frame[PAYLOAD_OFFSET],
               message->payload,
               message->payloadLength);
    }

    const size_t sequenceOffset = PAYLOAD_OFFSET + message->payloadLength;
    frame[sequenceOffset] = static_cast<uint8_t>(message->sequence >> 8U);
    frame[sequenceOffset + 1U] = static_cast<uint8_t>(message->sequence);

    const size_t crcOffset = sequenceOffset + SEQUENCE_SIZE;
    const uint16_t crc = calculateCrc(frame, crcOffset);
    frame[crcOffset] = static_cast<uint8_t>(crc >> 8U);
    frame[crcOffset + 1U] = static_cast<uint8_t>(crc);

    // Commit both outputs only after the complete frame has been constructed.
    memcpy(output, frame, frameLength);
    *outputLength = frameLength;
    return true;
}

bool decodePacket(const uint8_t *input,
                  size_t inputLength,
                  PacketMessage *message)
{
    if (input == nullptr || message == nullptr)
    {
        return false;
    }

    // The length guard makes every later access to the five-byte header, the
    // sequence, and the CRC safe.
    if (inputLength < MIN_FRAME_LENGTH || inputLength > MAX_FRAME_LENGTH)
    {
        return false;
    }

    const uint8_t payloadLength = input[PAYLOAD_LENGTH_OFFSET];
    if (payloadLength > MAX_PAYLOAD_LENGTH ||
        inputLength != MIN_FRAME_LENGTH + payloadLength)
    {
        return false;
    }

    const size_t sequenceOffset = PAYLOAD_OFFSET + payloadLength;
    const size_t crcOffset = sequenceOffset + SEQUENCE_SIZE;
    const uint16_t receivedCrc =
        static_cast<uint16_t>(input[crcOffset]) << 8U |
        static_cast<uint16_t>(input[crcOffset + 1U]);

    // CRC is checked before the received bytes can influence application state.
    if (receivedCrc != calculateCrc(input, crcOffset))
    {
        return false;
    }

    PacketMessage decoded{};
    decoded.type = static_cast<PacketType>(input[TYPE_OFFSET]);
    decoded.source = input[SOURCE_OFFSET];
    decoded.destination = input[DESTINATION_OFFSET];
    decoded.transactionId = input[TRANSACTION_ID_OFFSET];
    decoded.payloadLength = payloadLength;

    if (payloadLength > 0U)
    {
        memcpy(decoded.payload, &input[PAYLOAD_OFFSET], payloadLength);
    }

    decoded.sequence =
        static_cast<uint16_t>(input[sequenceOffset]) << 8U |
        static_cast<uint16_t>(input[sequenceOffset + 1U]);

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
    if (message == nullptr || temperatureX10 == nullptr || humidityX10 == nullptr)
    {
        return false;
    }

    if (message->type != PacketType::Data ||
        message->payloadLength != DATA_PAYLOAD_LENGTH)
    {
        return false;
    }

    const uint16_t temperatureBits =
        static_cast<uint16_t>(message->payload[0]) << 8U |
        static_cast<uint16_t>(message->payload[1]);
    const uint16_t decodedHumidity =
        static_cast<uint16_t>(message->payload[2]) << 8U |
        static_cast<uint16_t>(message->payload[3]);

    // Convert two's-complement explicitly instead of relying on the
    // implementation-defined uint16_t-to-int16_t conversion above INT16_MAX.
    int32_t decodedTemperature = temperatureBits;
    if ((temperatureBits & 0x8000U) != 0U)
    {
        decodedTemperature -= 0x10000L;
    }

    *temperatureX10 = static_cast<int16_t>(decodedTemperature);
    *humidityX10 = decodedHumidity;
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
