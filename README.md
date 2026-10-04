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
The sensor list is defined in `src/app_config.cpp`; addresses `0x01` and `0x02`
are enabled with 10-second polling intervals. The registry accepts at most five
unique addresses, and every interval must be at least 1500 ms.

The binary gateway path can be selected at build time without editing source:

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
{ "T": 25.50, "H": 60.20 }
```

With `GATEWAY_V1_ENABLED=1`, the gateway sends binary POLL packets and handles
DATA or ERROR responses. Every valid response matching the active transaction
receives an ACK. A new, in-range DATA sample retains its node address, sequence,
fixed-point measurements, RSSI, SNR, and receive time in a 64-entry RAM queue.
ERROR responses are acknowledged without creating a sample. The loop serializes
at most one queued sample per iteration and hands it to the asynchronous MQTT
QoS 1 outbox. FIFO entries are removed only after that handoff succeeds; a
rejected handoff remains at the head and is retried after one second. The
gateway sends MQTT work only between LoRa transactions, so an ESP-IDF MQTT
outbox lock wait cannot interrupt an active POLL/response/ACK exchange. The
installed ESP-IDF 4.4.7 MQTT client has no hard outbox size setting; the adapter
limits estimated QoS 1 packet bytes to 8 KiB, including topic and MQTT headers.
That is a software ceiling on packet bytes, not a measured heap ceiling. The SDK
protects enqueue and outbox-size calls with an internal mutex, and its network
timeout is set to 100 ms; multiple operations can occur under one lock, so this
does not guarantee a strict bound on total call time. Enqueue and PUBACK totals
are logged as a periodic aggregate. Device timing and actual broker receipt
still need hardware verification. An accepted enqueue is not a broker PUBACK,
and the RAM queue/outbox are lost on gateway reset. The SDK source used for the
mutex review is [ESP-MQTT v4.4.7](https://github.com/espressif/esp-mqtt/blob/bb9c8af9d552b608dd3aabf9617bde757a538ebe/mqtt_client.c).

## Build and Upload

Open the project with PlatformIO, then run:

```bash
pio run
pio run --target upload
pio device monitor
```

The Serial Monitor baud rate is `115200`.
