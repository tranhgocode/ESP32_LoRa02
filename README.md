# ESP32 LoRa-02 Gateway

This project uses an ESP32 and a LoRa-02 module to send or receive LoRa packets. In receiver mode, valid temperature and humidity data is forwarded to ThingsBoard over MQTT.

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

Select the LoRa mode in `include/config.h`:

```cpp
#define LORA_MODE_TX 0  // 0: receiver, 1: transmitter
```

Both devices must use the same frequency, sync word, spreading factor, bandwidth, and coding rate.

## Telemetry Format

The receiver accepts messages such as:

```text
T:25.5,H:60.2
```

Valid data is published to ThingsBoard as:

```json
{ "temperature": 25.5, "humidity": 60.2 }
```

## Build and Upload

Open the project with PlatformIO, then run:

```bash
pio run
pio run --target upload
pio device monitor
```

The Serial Monitor baud rate is `115200`.
