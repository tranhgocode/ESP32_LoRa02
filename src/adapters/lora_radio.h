#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gateway
{
namespace adapter
{
// The transport capacity matches the maximum V1 frame. Protocol validation is
// intentionally left to GatewayCore; this adapter only bounds byte movement.
constexpr size_t LORA_RADIO_FRAME_CAPACITY = 64U;

/** Result of initializing the SPI bus and SX127x-compatible LoRa radio. */
enum class LoRaRadioInitResult : uint8_t
{
    Ok = 0U,
    RadioNotFound
};

/**
 * Immediate result of asking the driver to start an asynchronous transmission.
 *
 * Started means the bytes were accepted and a later serviceLoRaRadio() call
 * will report TransmitSucceeded or TransmitFailed. Other values mean no TX was
 * started, allowing main.cpp to report the matching coordinator TX failure
 * immediately without waiting for an event that cannot arrive.
 */
enum class LoRaTransmitStartResult : uint8_t
{
    Started = 0U,
    NotReady,
    Busy,
    InvalidArgument,
    DriverRejected
};

/** Observable radio events produced outside the DIO0 interrupt context. */
enum class LoRaRadioEventType : uint8_t
{
    None = 0U,
    TransmitSucceeded,
    TransmitFailed,
    FrameReceived,
    FrameDropped
};

/**
 * Fixed-size event passed from the hardware adapter to the application loop.
 *
 * FrameReceived contains exactly frameLength bytes plus the packet RSSI and SNR
 * measured by the driver. SNR remains in signed quarter-dB units so T08 can
 * copy it directly into CoordinatorEvent and TelemetrySample. FrameDropped uses
 * frameLength for the reported radio packet size but its frame bytes are not
 * valid and must never be sent to the protocol decoder.
 */
struct LoRaRadioEvent
{
    LoRaRadioEventType type = LoRaRadioEventType::None;
    uint8_t frame[LORA_RADIO_FRAME_CAPACITY] = {};
    size_t frameLength = 0U;
    int16_t rssi = 0;
    int8_t snrX4 = 0;
};

/**
 * Configure SPI, pins, radio parameters, hardware CRC and receive mode.
 * Initialization returns a status instead of blocking forever when the radio
 * is absent. A successful call leaves the adapter ready to receive or transmit.
 */
LoRaRadioInitResult initializeLoRaRadio();

/**
 * Start sending one byte frame without waiting for the radio to finish.
 *
 * The request is rejected for null/empty/oversized input, an uninitialized
 * radio, a transmission already in progress, or a short/failed driver write.
 * The input is copied into the SX127x FIFO before this function returns, so the
 * caller does not need to preserve the buffer while transmission is active.
 */
LoRaTransmitStartResult startLoRaTransmit(const uint8_t *frame,
                                          size_t frameLength);

/**
 * Service at most one TX completion, TX watchdog, or received frame.
 *
 * This function never waits for radio activity. It returns true only when a
 * complete event was assigned to output; false leaves the caller's output
 * unchanged. Oversized and short driver reads are fully drained, reported as
 * FrameDropped, and never exposed as a partial protocol frame.
 */
bool serviceLoRaRadio(LoRaRadioEvent *output);

/** Return true after successful initialization, including during active TX. */
bool isLoRaRadioReady();

/** Let main.cpp avoid issuing a second request while async TX is in progress. */
bool isLoRaTransmitPending();

/** Stable diagnostic label for initialization logs without dynamic allocation. */
const char *loRaRadioInitResultName(LoRaRadioInitResult result);

/** Stable diagnostic label for immediate TX-start failures. */
const char *loRaTransmitStartResultName(LoRaTransmitStartResult result);
} // namespace adapter
} // namespace gateway
