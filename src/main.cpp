#include <Arduino.h>
#include "adapters/lora_radio.h"
#include "adapters/mqtt_transport.h"
#include "adapters/uart_reporter.h"
#include "app/gateway_app.h"
namespace
{
enum class GatewayState
{
    Stopped,
    WaitingForMqtt,
    Listening
};

GatewayState state = GatewayState::Stopped;
constexpr uint32_t RADIO_STATUS_INTERVAL_MS = 10000U;
constexpr size_t SERIAL_TX_BUFFER_CAPACITY = 1024U;

struct RadioStatus
{
    uint32_t received = 0U;
    uint32_t dropped = 0U;
    uint32_t lastReportedAtMs = 0U;
};

RadioStatus radioStatus{};

void startLoRaAfterMqtt()
{
    handleMqtt();
    // The session counter advances only after a successful broker connection.
    if (mqttSession().generation == 0U)
    {
        return;
    }

    if (gateway::adapter::initializeLoRaRadio() != gateway::adapter::LoRaRadioInitResult::Ok)
    {
        Serial.println("Error: LoRa initialization failed");
        state = GatewayState::Stopped;
        return;
    }

    state = GatewayState::Listening;
    radioStatus = RadioStatus{};
    radioStatus.lastReportedAtMs = millis();
    Serial.println("Status: LoRa listening continuously");
}

void serviceRadio()
{
    using gateway::adapter::LoRaRadioEventType;
    gateway::adapter::LoRaRadioEvent event{};
    if (gateway::adapter::serviceLoRaRadio(&event))
    {
        if (event.type == LoRaRadioEventType::FrameReceived)
        {
            ++radioStatus.received;
        }
        else if (event.type == LoRaRadioEventType::FrameDropped)
        {
            ++radioStatus.dropped;
        }
        gateway::app::gateway_app::handleRadio(event, millis());
    }
}

void reportRadioStatusIfDue(uint32_t nowMs)
{
    if (nowMs - radioStatus.lastReportedAtMs < RADIO_STATUS_INTERVAL_MS)
    {
        return;
    }

    radioStatus.lastReportedAtMs = nowMs;
    gateway::adapter::reportRadioStatus(gateway::adapter::readLoRaOperatingMode(),
                                        radioStatus.received, radioStatus.dropped);
}

void serviceGateway()
{
    serviceRadio();
    const uint32_t nowMs = millis();
    gateway::app::gateway_app::servicePendingAck(nowMs);
    reportRadioStatusIfDue(nowMs);
    handleMqtt();
    gateway::app::gateway_app::serviceTelemetry(millis());
}
}

void setup()
{
    // Allow a complete JSON log line to fit in a single UART write.
    Serial.setTxBufferSize(SERIAL_TX_BUFFER_CAPACITY);
    Serial.begin(115200);
    state = GatewayState::Stopped;
    if (!gateway::app::gateway_app::initialize())
    {
        Serial.println("Error: Invalid sensor registry");
        return;
    }
    initMqtt();
    state = GatewayState::WaitingForMqtt;
}

void loop()
{
    if (state == GatewayState::WaitingForMqtt)
    {
        startLoRaAfterMqtt();
    }
    else if (state == GatewayState::Listening)
    {
        // An MQTT outage after startup does not interrupt LoRa reception.
        serviceGateway();
    }
}
