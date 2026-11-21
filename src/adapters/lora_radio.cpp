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
// SX127x LoRa registers used to poll events and move complete FIFO frames.
constexpr uint8_t REG_FIFO = 0x00U;
constexpr uint8_t REG_OP_MODE = 0x01U;
constexpr uint8_t REG_FIFO_POINTER = 0x0DU;
constexpr uint8_t REG_FIFO_TX_BASE = 0x0EU;
constexpr uint8_t REG_FIFO_RX_CURRENT = 0x10U;
constexpr uint8_t REG_IRQ_FLAGS = 0x12U;
constexpr uint8_t REG_RX_LENGTH = 0x13U;
constexpr uint8_t REG_MODEM_CONFIG_1 = 0x1DU;
constexpr uint8_t REG_TX_LENGTH = 0x22U;

constexpr uint8_t SPI_WRITE = 0x80U;
constexpr uint8_t LORA_MODE = 0x80U;
constexpr uint8_t LOW_FREQUENCY_MODE = 0x08U;
constexpr uint8_t IMPLICIT_HEADER = 0x01U;
constexpr uint8_t IRQ_RX_DONE = 0x40U;
constexpr uint8_t IRQ_PAYLOAD_CRC_ERROR = 0x20U;
constexpr uint8_t IRQ_TX_DONE = 0x08U;
constexpr uint8_t IRQ_CLEAR_ALL = 0xFFU;
constexpr uint32_t RADIO_SPI_HZ = 8000000U;

enum class RadioMode : uint8_t
{
    Sleep = 0x00U,
    Standby = 0x01U,
    Tx = 0x03U,
    ContinuousRx = 0x05U
};

enum class RadioState : uint8_t
{
    Uninitialized,
    Receiving,
    Transmitting
};

RadioState radioState = RadioState::Uninitialized;
uint32_t transmitStartedAtMs = 0U;

void selectRegister(uint8_t command)
{
    SPI.beginTransaction(SPISettings(RADIO_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_NSS, LOW);
    SPI.transfer(command);
}

void releaseRadio()
{
    digitalWrite(LORA_NSS, HIGH);
    SPI.endTransaction();
}

uint8_t readRegister(uint8_t address)
{
    selectRegister(address);
    const uint8_t value = SPI.transfer(0U);
    releaseRadio();
    return value;
}

void writeRegister(uint8_t address, uint8_t value)
{
    selectRegister(address | SPI_WRITE);
    SPI.transfer(value);
    releaseRadio();
}

void setRadioMode(RadioMode mode)
{
    // Preserve the same 433 MHz operating mode as ESP32_NodeSensor.
    writeRegister(REG_OP_MODE, LORA_MODE | LOW_FREQUENCY_MODE | static_cast<uint8_t>(mode));
}

void startReceive()
{
    setRadioMode(RadioMode::ContinuousRx);
    radioState = RadioState::Receiving;
}

void readFrame(uint8_t *frame, size_t length)
{
    writeRegister(REG_FIFO_POINTER, readRegister(REG_FIFO_RX_CURRENT));
    selectRegister(REG_FIFO);
    for (size_t index = 0U; index < length; ++index)
    {
        frame[index] = SPI.transfer(0U);
    }
    releaseRadio();
}

void writeFrame(const uint8_t *frame, size_t length)
{
    writeRegister(REG_FIFO_POINTER, readRegister(REG_FIFO_TX_BASE));
    selectRegister(REG_FIFO | SPI_WRITE);
    for (size_t index = 0U; index < length; ++index)
    {
        SPI.transfer(frame[index]);
    }
    releaseRadio();
    writeRegister(REG_TX_LENGTH, static_cast<uint8_t>(length));
}

int16_t clampRssi(int value)
{
    if (value < INT16_MIN)
    {
        return INT16_MIN;
    }
    if (value > INT16_MAX)
    {
        return INT16_MAX;
    }
    return static_cast<int16_t>(value);
}

int8_t readSnrX4()
{
    const int value = static_cast<int>(LoRa.packetSnr() * 4.0F);
    if (value < INT8_MIN)
    {
        return INT8_MIN;
    }
    if (value > INT8_MAX)
    {
        return INT8_MAX;
    }
    return static_cast<int8_t>(value);
}

LoRaRadioEvent finishReceive(uint8_t flags)
{
    // Freeze the completed FIFO frame while reading it, then resume reception.
    setRadioMode(RadioMode::Standby);
    writeRegister(REG_IRQ_FLAGS, flags);

    LoRaRadioEvent event{};
    event.frameLength = readRegister(REG_RX_LENGTH);
    if ((flags & IRQ_PAYLOAD_CRC_ERROR) != 0U ||
        event.frameLength == 0U || event.frameLength > LORA_RADIO_FRAME_CAPACITY)
    {
        event.type = LoRaRadioEventType::FrameDropped;
    }
    else
    {
        event.type = LoRaRadioEventType::FrameReceived;
        event.rssi = clampRssi(LoRa.packetRssi());
        event.snrX4 = readSnrX4();
        readFrame(event.frame, event.frameLength);
    }
    startReceive();
    return event;
}

bool serviceTransmit(uint8_t flags, LoRaRadioEvent &event)
{
    const bool finished = (flags & IRQ_TX_DONE) != 0U;
    const uint32_t now = millis();
    if (!finished && now - transmitStartedAtMs < LORA_TX_TIMEOUT_MS)
    {
        return false;
    }

    setRadioMode(RadioMode::Standby);
    writeRegister(REG_IRQ_FLAGS, IRQ_CLEAR_ALL);
    startReceive();
    event.type = finished ? LoRaRadioEventType::TransmitSucceeded
                          : LoRaRadioEventType::TransmitFailed;
    return true;
}

void configureModem()
{
    setRadioMode(RadioMode::Sleep);
    setRadioMode(RadioMode::Standby);
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_SIGNAL_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE_DENOMINATOR);
    LoRa.setPreambleLength(LORA_PREAMBLE_SYMBOLS);
    LoRa.disableInvertIQ();
    LoRa.enableCrc();
    LoRa.setSyncWord(LORA_SYNC_WORD);
    writeRegister(REG_MODEM_CONFIG_1, readRegister(REG_MODEM_CONFIG_1) & ~IMPLICIT_HEADER);
    writeRegister(REG_IRQ_FLAGS, IRQ_CLEAR_ALL);
}
} // namespace

LoRaRadioInitResult initializeLoRaRadio()
{
    radioState = RadioState::Uninitialized;
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RESET, LORA_DIO0);
    if (LoRa.begin(LORA_FREQUENCY) == 0)
    {
        return LoRaRadioInitResult::RadioNotFound;
    }

    configureModem();
    startReceive();
    return LoRaRadioInitResult::Ok;
}

LoRaTransmitStartResult startLoRaTransmit(const uint8_t *frame, size_t frameLength)
{
    if (frame == nullptr || frameLength == 0U || frameLength > LORA_RADIO_FRAME_CAPACITY)
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

    setRadioMode(RadioMode::Standby);
    writeRegister(REG_IRQ_FLAGS, IRQ_CLEAR_ALL);
    writeFrame(frame, frameLength);
    transmitStartedAtMs = millis();
    radioState = RadioState::Transmitting;
    setRadioMode(RadioMode::Tx);
    return LoRaTransmitStartResult::Started;
}

bool serviceLoRaRadio(LoRaRadioEvent *output)
{
    if (output == nullptr || radioState == RadioState::Uninitialized)
    {
        return false;
    }

    // Read only IRQ flags while listening so RX stays in continuous mode.
    const uint8_t flags = readRegister(REG_IRQ_FLAGS);
    if (radioState == RadioState::Transmitting)
    {
        LoRaRadioEvent event{};
        if (!serviceTransmit(flags, event))
        {
            return false;
        }
        *output = event;
        return true;
    }
    if ((flags & IRQ_RX_DONE) == 0U)
    {
        return false;
    }

    *output = finishReceive(flags);
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

uint8_t readLoRaOperatingMode()
{
    return readRegister(REG_OP_MODE);
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
