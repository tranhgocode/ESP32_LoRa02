#include <Arduino.h>
#include "config.h"

unsigned long previousBlinkTime = 0;
bool ledState = LOW;

void setup()
{
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);

  Serial.println("LED blink started");
}

void loop()
{
  const unsigned long currentTime = millis();

  if (currentTime - previousBlinkTime >= LED_BLINK_INTERVAL_MS)
  {
    previousBlinkTime = currentTime;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }
}
