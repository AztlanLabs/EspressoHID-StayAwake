#include "human_input.h"

unsigned long humanDelayMs(int wpmMin, int wpmMax) {
  const int wpm = random(wpmMin, wpmMax + 1);
  const long base = 60000L / (wpm * 5L);          // ms per character
  const long jitter = random(-base / 4, base / 4); // ±25%
  return (unsigned long)max(20L, base + jitter);
}

void tapKey(USBHIDKeyboard& keyboard, uint8_t key,
            uint16_t holdMinMs, uint16_t holdMaxMs,
            uint16_t gapMinMs, uint16_t gapMaxMs) {
  keyboard.press(key);
  delay(random(holdMinMs, holdMaxMs + 1));
  keyboard.releaseAll();
  delay(random(gapMinMs, gapMaxMs + 1));
}

void chordKey(USBHIDKeyboard& keyboard, const uint8_t* keys, size_t keyCount,
              uint16_t downGapMinMs, uint16_t downGapMaxMs,
              uint16_t holdMinMs, uint16_t holdMaxMs) {
  for (size_t i = 0; i < keyCount; i++) {
    keyboard.press(keys[i]);
    delay(random(downGapMinMs, downGapMaxMs + 1));
  }
  delay(random(holdMinMs, holdMaxMs + 1));
  keyboard.releaseAll();
}

void typeTextHuman(USBHIDKeyboard& keyboard, const char* text, int wpmMin, int wpmMax) {
  if (!text) return;
  for (size_t i = 0; text[i] != '\0'; i++) {
    keyboard.print(text[i]);
    delay(humanDelayMs(wpmMin, wpmMax));

    // occasional micro pause
    if (random(100) < 4) delay(random(120, 320));
  }
}

void backspaceHuman(USBHIDKeyboard& keyboard, size_t count) {
  for (size_t i = 0; i < count; i++) {
    tapKey(keyboard, KEY_BACKSPACE, 25, 65, 30, 110);
    if (random(100) < 5) delay(random(80, 200));
  }
}

void tapConsumer(USBHIDConsumerControl& consumer, uint16_t code,
                 uint16_t holdMinMs, uint16_t holdMaxMs) {
  consumer.press(code);
  delay(random(holdMinMs, holdMaxMs + 1));
  consumer.release();
}
