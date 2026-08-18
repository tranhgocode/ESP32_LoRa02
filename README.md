# ESP32 + LoRa-02

Chuong trinh thu gui/nhan LoRa bang thu vien `sandeepmistry/LoRa@^0.8.0` va PlatformIO.

## Ket noi

| LoRa-02 | ESP32 | Chuc nang |
| --- | --- | --- |
| 3.3V | 3V3 | Nguon 3.3 V |
| GND | GND | Mass |
| SCK | GPIO18 | SPI clock |
| MISO | GPIO19 | LoRa -> ESP32 |
| MOSI | GPIO23 | ESP32 -> LoRa |
| NSS / CS | GPIO27 | SPI chip select |
| RESET | GPIO14 | Reset LoRa |
| DIO0 | GPIO26 | Ngat RxDone / TxDone |

> Chi cap LoRa-02 bang **3.3 V**, khong dung 5 V. Nen gan anten phu hop truoc khi phat.

## Chay thu

1. Trong `include/config.h`, dat `LORA_MODE_TX` la `1` cho bo phat, hoac `0` cho bo nhan.
2. Cam ESP32, sau do chay **PlatformIO: Upload**.
3. Mo **PlatformIO: Serial Monitor** o toc do `115200` baud.
4. Bo phat gui mot goi moi 2 giay; bo nhan hien noi dung, RSSI va SNR.

De thu truyen thuc te, can hai bo ESP32 + LoRa-02: nap mot bo o che do phat va mot bo o che do nhan. Ca hai phai co cung tan so; mac dinh trong `include/config.h` la `433E6` (433 MHz).
