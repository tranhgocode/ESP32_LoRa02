#include <Arduino.h>

#include "adapters/lora_radio.h"
#include "adapters/mqtt_transport.h"
#include "app_config.h"

#if GATEWAY_UPLINK_ENABLED
#include "app/uplink_uart_mode.h"
namespace activeMode = gateway::app::uplink_uart_mode;
#elif GATEWAY_V1_ENABLED
#include "app/poll_mode.h"
namespace activeMode = gateway::app::poll_mode;
#else
#include "app/legacy_mode.h"
namespace activeMode = gateway::app::legacy_mode;
#endif

namespace
{
#if GATEWAY_V1_ENABLED
bool gatewayV1Ready = false;
#endif

/** Poll the adapter once and process at most one event without waiting. */
void serviceRadio()
{
    gateway::adapter::LoRaRadioEvent event{};
    if (!gateway::adapter::serviceLoRaRadio(&event))
    {
        return;
    }

#if GATEWAY_V1_ENABLED
    if (gatewayV1Ready)
    {
        activeMode::handleRadio(event, millis());
    }
#else
    activeMode::handleRadio(event);
#endif
}
} // namespace

void setup()
{
    // Start diagnostics before initializing network and radio services.
    Serial.begin(115200);
    delay(1000);

    // MQTT startup requests Wi-Fi/client connection asynchronously.
#if !GATEWAY_UPLINK_ENABLED
    initMqtt();
#endif

#if GATEWAY_V1_ENABLED
    gatewayV1Ready = false;
    if (!activeMode::initialize(millis()))
    {
        return;
    }
#endif

    Serial.print("Status: Starting LoRa at ");
    Serial.print(LORA_FREQUENCY / 1000000L);
    Serial.println(" MHz...");

    const gateway::adapter::LoRaRadioInitResult radioResult =
        gateway::adapter::initializeLoRaRadio();
    if (radioResult != gateway::adapter::LoRaRadioInitResult::Ok)
    {
        Serial.print("Error: LoRa initialization failed: ");
        Serial.println(gateway::adapter::loRaRadioInitResultName(radioResult));
        return;
    }

    Serial.println("Status: LoRa is ready in receive mode");
#if GATEWAY_V1_ENABLED
    gatewayV1Ready = true;
#if GATEWAY_UPLINK_ENABLED
    Serial.println("Mode: UPLINK_UART, node 0x01, record then ACK");
#else
    Serial.println("Mode: Gateway protocol V1, telemetry queued to MQTT");
#endif
#else
    Serial.println("Mode: Legacy text telemetry compatibility");
#endif
}

void loop()
{
    // Service radio first so TX completion and received DATA reach the
    // coordinator with minimum loop-induced delay.
    serviceRadio();

    // Wi-Fi retry and MQTT network I/O are asynchronous.
#if !GATEWAY_UPLINK_ENABLED
    handleMqtt();
#if GATEWAY_V1_ENABLED
    activeMode::serviceTelemetry();
    // Start new LoRa work after the idle-only MQTT outbox handoff.
    if (gatewayV1Ready)
    {
        activeMode::serviceTick(millis());
    }
#endif
#endif
}
