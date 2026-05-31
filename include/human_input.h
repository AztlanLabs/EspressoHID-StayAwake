#pragma once

#include <Arduino.h>
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

// Reusable helpers for human-like key timing.

unsigned long humanDelayMs(int wpmMin, int wpmMax);

void tapKey(USBHIDKeyboard& keyboard, uint8_t key,
            uint16_t holdMinMs = 30, uint16_t holdMaxMs = 90,
            uint16_t gapMinMs = 40, uint16_t gapMaxMs = 140);

void chordKey(USBHIDKeyboard& keyboard, const uint8_t* keys, size_t keyCount,
              uint16_t downGapMinMs = 20, uint16_t downGapMaxMs = 70,
              uint16_t holdMinMs = 40, uint16_t holdMaxMs = 120);

// Types text, then (optionally) deletes it with backspace count.
void typeTextHuman(USBHIDKeyboard& keyboard, const char* text,
                   int wpmMin = 55, int wpmMax = 115);

void backspaceHuman(USBHIDKeyboard& keyboard, size_t count);

// Consumer control tap (+release)
void tapConsumer(USBHIDConsumerControl& consumer, uint16_t code,
                 uint16_t holdMinMs = 30, uint16_t holdMaxMs = 90);
