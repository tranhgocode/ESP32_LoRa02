#include <Arduino.h>
#include "adapters/lora_radio.h"
#include "adapters/mqtt_transport.h"
#include "app/gateway_app.h"
namespace
{
bool gatewayReady = false;
}
void setup()
{
    Serial.begin(115200);
    gatewayReady = false;
    if (!gateway::app::gateway_app::initialize())
    {
        Serial.println("Error: Invalid sensor registry");
        return;
    }
    gatewayReady = gateway::adapter::initializeLoRaRadio() ==
        gateway::adapter::LoRaRadioInitResult::Ok;
    if (!gatewayReady) Serial.println("Error: LoRa initialization failed");
    initMqtt();
}
void loop()
{
    gateway::adapter::LoRaRadioEvent event{};
    if (gateway::adapter::serviceLoRaRadio(&event) && gatewayReady)
        gateway::app::gateway_app::handleRadio(event, millis());
    handleMqtt();
    if (gatewayReady) gateway::app::gateway_app::serviceTelemetry(millis());
}
