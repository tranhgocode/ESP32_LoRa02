#pragma once

// Initialize the LoRa module and select transmit or receive mode.
void initLoRa();

// Process LoRa transmission or reception; call continuously from loop().
void handleLoRa();
