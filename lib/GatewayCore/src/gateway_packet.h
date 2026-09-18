#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gateway
{
// Reserved addresses and frame limits are part of the V1 wire contract. Keeping
// them beside the codec prevents the ESP32 adapter and tests from defining
// slightly different values.
constexpr uint8_t GATEWAY_ADDRESS = 0x00U;
constexpr uint8_t RESERVED_ADDRESS = 0xFFU;
constexpr uint8_t MIN_NODE_ADDRESS = 0x01U;
constexpr uint8_t MAX_NODE_ADDRESS = 0xFEU;
constexpr size_t MIN_FRAME_LENGTH = 9U;
constexpr size_t MAX_FRAME_LENGTH = 64U;
constexpr size_t MAX_PAYLOAD_LENGTH = 55U;
constexpr uint8_t DATA_PAYLOAD_LENGTH = 4U;
constexpr uint8_t ERROR_PAYLOAD_LENGTH = 1U;

/** Packet types supported by both the ESP32 gateway and the SAMD21 sensor. */
enum class PacketType : uint8_t
{
    Poll = 0x01U,
    Data = 0x02U,
    Ack = 0x03U,
    Error = 0x04U
};

/** Error values allowed in the single-byte payload of an ERROR packet. */
enum class PacketErrorCode : uint8_t
{
    DhtInitFailed = 0x01U,
    DhtReadFailed = 0x02U,
    DhtValueInvalid = 0x03U,
    PacketBuildFailed = 0x04U
};

/**
 * In-memory representation of one V1 packet.
 *
 * The struct itself is never sent over radio because compiler padding and CPU
 * byte order are not a wire format. encodePacket() serializes every field
 * explicitly; decodePacket() performs the reverse operation after validating
 * the complete frame.
 */
struct PacketMessage
{
    PacketType type = PacketType::Poll;
    uint8_t source = GATEWAY_ADDRESS;
    uint8_t destination = GATEWAY_ADDRESS;
    uint8_t transactionId = 0U;
    uint8_t payloadLength = 0U;
    uint8_t payload[MAX_PAYLOAD_LENGTH] = {};
    uint16_t sequence = 0U;
};

/**
 * Validate and serialize a packet as
 * Type|Src|Dest|TransactionId|Len|Payload|Sequence|CRC16.
 *
 * Multi-byte fields are written big-endian and CRC-16/CCITT-FALSE covers all
 * bytes except the CRC field itself. The function builds into a local temporary
 * frame first, so output and outputLength remain unchanged on every failure.
 *
 * @return true only when a complete valid frame was copied to output.
 */
bool encodePacket(const PacketMessage *message,
                  uint8_t *output,
                  size_t outputCapacity,
                  size_t *outputLength);

/**
 * Validate a complete V1 frame and decode it into PacketMessage.
 *
 * Validation covers pointer/length safety, declared payload length, CRC, packet
 * type, direction, addresses, payload contract, and ERROR code. Decoding uses a
 * local message and assigns it to the caller only after every check succeeds,
 * leaving the caller's message unchanged when the frame is rejected.
 */
bool decodePacket(const uint8_t *input,
                  size_t inputLength,
                  PacketMessage *message);

/**
 * Read the fixed-point values from an already-decoded DATA packet.
 *
 * Temperature is a signed two's-complement int16 multiplied by 10; humidity is
 * an unsigned uint16 multiplied by 10. Both fields are big-endian. This helper
 * checks type and payload length and leaves both outputs unchanged on failure.
 */
bool decodeDataPayload(const PacketMessage *message,
                       int16_t *temperatureX10,
                       uint16_t *humidityX10);

/**
 * Read and validate the single error code from an already-decoded ERROR packet.
 * The output remains unchanged if the pointer, type, length, or code is invalid.
 */
bool decodeErrorPayload(const PacketMessage *message,
                        PacketErrorCode *errorCode);
} // namespace gateway
