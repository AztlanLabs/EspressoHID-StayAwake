#include "button_handler.h"
#include "state.h"
#include "profiles.h"
#include "led_controller.h"
#include "control.h"
#include "runtime_config.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

extern USBHIDKeyboard        Keyboard;
extern USBHIDConsumerControl Consumer;

// ---------------------------------------------------------------------------
//  Button Handling  (short press = toggle, long press = cycle profile)
// ---------------------------------------------------------------------------
void handleButton() {
  static bool lastReading          = HIGH;
  static bool buttonState          = HIGH;
  static unsigned long lastBounceMs = 0;
  static unsigned long pressStartMs = 0;
  static bool longHandled          = false;
  static bool resetHandled         = false;

  const bool reading         = digitalRead(PIN_BUTTON);
  const unsigned long now    = millis();

  if (reading != lastReading) lastBounceMs = now;
  lastReading = reading;

  if ((now - lastBounceMs) < BUTTON_DEBOUNCE_MS) return;

  // Detect transitions
  if (reading != buttonState) {
    buttonState = reading;

    if (buttonState == LOW) {
      // Button just pressed
      pressStartMs = now;
      longHandled  = false;
      resetHandled = false;
    } else {
      // Button just released
      if (!longHandled) {
        // Short press: toggle active state
        (void)now;
        controlToggleActive();
      }
    }
    return;
  }

  // While held: factory reset
  if (buttonState == LOW && !resetHandled && (now - pressStartMs) >= FACTORY_RESET_HOLD_MS) {
    resetHandled = true;
    DEBUG_PRINTLN("[RESET] Factory reset (button hold)");
    // Best-effort: ensure NVS started
    runtimeConfigBegin();
    runtimeConfigFactoryReset();
    delay(300);
    ESP.restart();
    return;
  }

  // While held: detect long press to switch profile
  if (buttonState == LOW && !longHandled && (now - pressStartMs) >= LONG_PRESS_MS) {
    longHandled = true;

    // Cycle profile
    controlSetProfile((Profile)((currentProfile + 1) % PROFILE_COUNT));
  }
}
