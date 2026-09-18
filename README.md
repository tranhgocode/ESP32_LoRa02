# ESP32 LoRa-02 Gateway

This project uses an ESP32 and a LoRa-02 module as a gateway. The default transition build receives legacy text telemetry and forwards valid temperature and humidity data to ThingsBoard over MQTT. When the protocol V1 build flag is enabled, the gateway polls one statically configured sensor, acknowledges valid binary responses, and stores accepted DATA samples in a bounded RAM queue.

## Hardware

- ESP32 development board
- LoRa-02 433 MHz module
- 433 MHz antenna

## Wiring

| LoRa-02 pin | ESP32 pin | Purpose            |
| ----------- | --------: | ------------------ |
| VCC         |      3.3V | Power              |
| GND         |       GND | Ground             |
| SCK         |   GPIO 18 | SPI clock          |
| MISO        |   GPIO 19 | SPI data from LoRa |
| MOSI        |   GPIO 23 | SPI data to LoRa   |
| NSS / CS    |   GPIO 27 | SPI chip select    |
| RESET       |   GPIO 14 | Module reset       |
| DIO0        |   GPIO 26 | Interrupt signal   |

> Important: Power the LoRa-02 module with **3.3 V only** and connect the antenna before transmitting.

## Configuration

Copy `.env.example` to `.env`, then enter your Wi-Fi and ThingsBoard settings:

```env
WIFI_SSID=YOUR_WIFI_SSID
WIFI_PASSWORD=YOUR_WIFI_PASSWORD
MQTT_HOST=YOUR_MQTT_HOST
MQTT_PORT=1883
MQTT_CLIENT_ID=YOUR_MQTT_CLIENT_ID
MQTT_USERNAME=YOUR_MQTT_USERNAME
MQTT_PASSWORD=YOUR_MQTT_PASSWORD
MQTT_TELEMETRY_TOPIC=v1/devices/me/telemetry
```

Do not commit `.env` because it contains private credentials.

Radio pins and physical-layer parameters are defined in `include/app_config.h`.
The sensor list is defined in `src/app_config.cpp`; T08 enables address `0x01`
with a 10-second polling interval. The registry accepts at most five unique
addresses, and every interval must be at least 1500 ms.

The binary gateway path remains disabled by default until radio timing is
verified on hardware. Override the flag at build time without editing source:

```powershell
$env:PLATFORMIO_BUILD_FLAGS = "-DGATEWAY_V1_ENABLED=1"
pio run -e esp32dev
Remove-Item Env:PLATFORMIO_BUILD_FLAGS
```

Both devices must use the same frequency, sync word, spreading factor, bandwidth, and coding rate.

## Telemetry Format

With `GATEWAY_V1_ENABLED=0`, the compatibility receiver accepts messages such as:

```text
T:25.5,H:60.2
```

Valid data is published to ThingsBoard as:

```json
{ "temperature": 25.5, "humidity": 60.2 }
```

With `GATEWAY_V1_ENABLED=1`, the gateway sends binary POLL packets and handles
DATA or ERROR responses. Every valid response matching the active transaction
receives an ACK. A new, in-range DATA sample retains its node address, sequence,
fixed-point measurements, RSSI, SNR, and receive time in a 64-entry RAM queue.
ERROR responses are acknowledged without creating a sample. MQTT delivery from
this V1 queue is planned for a later task, so queued samples are currently lost
when the gateway resets.

## Build and Upload

Open the project with PlatformIO, then run:

```bash
pio run
pio run --target upload
pio device monitor
```

The Serial Monitor baud rate is `115200`.
