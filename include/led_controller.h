#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>

void updateLed();
void setLed(uint8_t r, uint8_t g, uint8_t b);
void setChargeLed(float progress);

// Manual feedback blink (does not alter ACTIVE scheduling state).
void ledManualBlinkGreen(unsigned long durationMs);

#endif // LED_CONTROLLER_H
