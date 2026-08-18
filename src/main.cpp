#include <Arduino.h>
#include "my_lora.h"

void setup()
{
  Serial.begin(115200);
  delay(1000);

  initLoRa();
}

void loop()
{
  handleLoRa();
}
