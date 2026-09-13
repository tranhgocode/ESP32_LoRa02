#include "lora_radio.h"

#include "app_config.h"

#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>

#include <limits.h>

namespace gateway
{
namespace adapter
{
namespace
{
/** Internal ownership state prevents concurrent use of the single SX127x FIFO. */
enum class RadioState : uint8_t
{
    Uninitialized = 0U,
    Receiving,
    Transmitting
};

RadioState radioState = RadioState::Uninitialized;
volatile bool transmitDoneInterrupt = false;
uint32_t transmitStartedAtMs = 0U;

/**
 * DIO0 callbacks run in interrupt context in arduino-LoRa 0.8.0. The callback
 * therefore records only a volatile flag; SPI access, state transitions, and
 * event construction remain in serviceLoRaRadio() in the application loop.
 *
 * Source: https://github.com/sandeepmistry/arduino-LoRa/blob/0.8.0/API.md#tx-done
 */
void IRAM_ATTR handleTransmitDoneInterrupt()
{
    transmitDoneInterrupt = true;
}

/**
 * Detach the TX callback before mapping DIO0 back to RXDONE. Leaving the
 * callback attached would let the library ISR clear an RX interrupt before the
 * polling path can observe it through parsePacket().
 */
void restoreReceiveMode()
{
    LoRa.onTxDone(nullptr);
    LoRa.receive();
    radioState = RadioState::Receiving;
}

/** Clamp the driver's integer RSSI to the fixed event representation. */
int16_t clampRssi(int rssi)
{
    if (rssi < INT16_MIN)
    {
        return INT16_MIN;
    }

    if (rssi > INT16_MAX)
    {
        return INT16_MAX;
    }

    return static_cast<int16_t>(rssi);
}

/**
 * arduino-LoRa reports SNR in exact 0.25 dB steps. Scaling by four preserves
 * the signed register value used by GatewayCore and avoids float state in the
 * event/action boundary.
 */
int8_t readSnrX4()
{
    const int scaledSnr = static_cast<int>(LoRa.packetSnr() * 4.0F);
    if (scaledSnr < INT8_MIN)
    {
        return INT8_MIN;
    }

    if (scaledSnr > INT8_MAX)
    {
        return INT8_MAX;
    }

    return static_cast<int8_t>(scaledSnr);
}

/**
 * Drain the entire FIFO once parsePacket() reports a packet. Only an exact,
 * bounded read becomes FrameReceived; every mismatch becomes FrameDropped so
 * no stale or partial bytes can reach the V1 decoder.
 */
LoRaRadioEvent readReceivedFrame(int reportedLength)
{
    LoRaRadioEvent event{};
    event.frameLength = static_cast<size_t>(reportedLength);
    event.rssi = clampRssi(LoRa.packetRssi());
    event.snrX4 = readSnrX4();

    size_t bytesRead = 0U;
    while (LoRa.available() > 0)
    {
        const int value = LoRa.read();
        if (value < 0)
        {
            break;
        }

        if (bytesRead < LORA_RADIO_FRAME_CAPACITY)
        {
            event.frame[bytesRead] = static_cast<uint8_t>(value);
        }
        ++bytesRead;
    }

    // parsePacket() moves the radio to standby for a completed packet. Restore
    // RX immediately after draining so processing in main cannot create a deaf
    // interval longer than this bounded service call.
    LoRa.receive();

    const bool completeBoundedFrame =
        event.frameLength <= LORA_RADIO_FRAME_CAPACITY &&
        bytesRead == event.frameLength;
    event.type = completeBoundedFrame
                     ? LoRaRadioEventType::FrameReceived
                     : LoRaRadioEventType::FrameDropped;
    return event;
}
} // namespace

LoRaRadioInitResult initializeLoRaRadio()
{
    // Preserve the deployed SPI pins and register DIO0 before LoRa.begin(), as
    // required by the library's pin configuration API.
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);

    transmitDoneInterrupt = false;
    transmitStartedAtMs = 0U;
    radioState = RadioState::Uninitialized;

    if (LoRa.begin(LORA_FREQUENCY) == 0)
    {
        return LoRaRadioInitResult::RadioNotFound;
    }

    // These values intentionally match the previous gateway and sensor radio
    // contract. Hardware CRC filters damaged physical packets before the V1
    // application CRC performs its own end-to-end frame validation.
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_SIGNAL_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE_DENOMINATOR);
    LoRa.enableCrc();
    LoRa.setSyncWord(LORA_SYNC_WORD);

    restoreReceiveMode();
    return LoRaRadioInitResult::Ok;
}

LoRaTransmitStartResult startLoRaTransmit(const uint8_t *frame,
                                          size_t frameLength)
{
    if (frame == nullptr || frameLength == 0U ||
        frameLength > LORA_RADIO_FRAME_CAPACITY)
    {
        return LoRaTransmitStartResult::InvalidArgument;
    }

    if (radioState == RadioState::Uninitialized)
    {
        return LoRaTransmitStartResult::NotReady;
    }

    if (radioState == RadioState::Transmitting)
    {
        return LoRaTransmitStartResult::Busy;
    }

    // beginPacket() returns zero if the radio is busy. write() returns the exact
    // accepted byte count, so a short write is detected before TX is started.
    if (LoRa.beginPacket() == 0)
    {
        restoreReceiveMode();
        return LoRaTransmitStartResult::DriverRejected;
    }

    if (LoRa.write(frame, frameLength) != frameLength)
    {
        LoRa.idle();
        restoreReceiveMode();
        return LoRaTransmitStartResult::DriverRejected;
    }

    transmitDoneInterrupt = false;
    LoRa.onTxDone(handleTransmitDoneInterrupt);
    radioState = RadioState::Transmitting;
    transmitStartedAtMs = millis();

    // async=true is the documented nonblocking mode. The registered DIO0
    // callback turns completion into a flag that serviceLoRaRadio() consumes.
    // Source: https://github.com/sandeepmistry/arduino-LoRa/blob/0.8.0/API.md#end-packet
    if (LoRa.endPacket(true) == 0)
    {
        LoRa.idle();
        transmitDoneInterrupt = false;
        restoreReceiveMode();
        return LoRaTransmitStartResult::DriverRejected;
    }

    return LoRaTransmitStartResult::Started;
}

bool serviceLoRaRadio(LoRaRadioEvent *output)
{
    if (output == nullptr || radioState == RadioState::Uninitialized)
    {
        return false;
    }

    LoRaRadioEvent event{};

    if (radioState == RadioState::Transmitting)
    {
        // Completion wins if it arrives at the watchdog boundary. This avoids
        // reporting a false failure when DIO0 was already observed by the ISR.
        if (transmitDoneInterrupt)
        {
            transmitDoneInterrupt = false;
            restoreReceiveMode();
            event.type = LoRaRadioEventType::TransmitSucceeded;
            *output = event;
            return true;
        }

        if (millis() - transmitStartedAtMs >= LORA_TX_TIMEOUT_MS)
        {
            // Stop a TX whose DIO0 completion was never observed, detach the
            // interrupt callback, and return to RX before reporting failure.
            LoRa.idle();
            transmitDoneInterrupt = false;
            restoreReceiveMode();
            event.type = LoRaRadioEventType::TransmitFailed;
            *output = event;
            return true;
        }

        return false;
    }

    // parsePacket() only inspects radio state/registers and returns zero when no
    // packet is ready; it does not wait for a future packet.
    const int packetLength = LoRa.parsePacket();
    if (packetLength <= 0)
    {
        return false;
    }

    event = readReceivedFrame(packetLength);
    *output = event;
    return true;
}

bool isLoRaRadioReady()
{
    return radioState != RadioState::Uninitialized;
}

bool isLoRaTransmitPending()
{
    return radioState == RadioState::Transmitting;
}

const char *loRaRadioInitResultName(LoRaRadioInitResult result)
{
    switch (result)
    {
    case LoRaRadioInitResult::Ok:
        return "ok";
    case LoRaRadioInitResult::RadioNotFound:
        return "radio_not_found";
    default:
        return "unknown";
    }
}

const char *loRaTransmitStartResultName(LoRaTransmitStartResult result)
{
    switch (result)
    {
    case LoRaTransmitStartResult::Started:
        return "started";
    case LoRaTransmitStartResult::NotReady:
        return "not_ready";
    case LoRaTransmitStartResult::Busy:
        return "busy";
    case LoRaTransmitStartResult::InvalidArgument:
        return "invalid_argument";
    case LoRaTransmitStartResult::DriverRejected:
        return "driver_rejected";
    default:
        return "unknown";
    }
}
} // namespace adapter
} // namespace gateway
