// ============================================================================
//  Fake Keyboard — Main Firmware
//  ESP32-S3 HID jiggler that masquerades as a standard keyboard.
//  All tunables live in include/config.h — edit there, not here.
// ============================================================================

#include <Arduino.h>
#include <fake_keyboard_core.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "config.h"
#include "state.h"
#include "profiles.h"
#include "led_controller.h"
#include "usb_identity.h"
#include "actions.h"
#include "sleep_manager.h"
#include "button_handler.h"
#include "web_portal.h"

// ---------------------------------------------------------------------------
//  HID Instances
// ---------------------------------------------------------------------------
USBHIDKeyboard       Keyboard;
USBHIDConsumerControl Consumer;

// ---------------------------------------------------------------------------
//  Setup
// ---------------------------------------------------------------------------
void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  DEBUG_BEGIN(115200);
  delay(100);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("[BOOT] Fake Keyboard starting...");

  setLed(0, 0, LED_BLUE_IDLE);

  for (int i = BOOT_COUNTDOWN_S; i >= 1; --i) {
    DEBUG_PRINT("[BOOT] USB in ");
    DEBUG_PRINT(i);
    DEBUG_PRINTLN("s");
    delay(1000);
  }

  applyRandomIdentity();

  Keyboard.begin();
  Consumer.begin();
  USB.begin();

  ledMode = LED_IDLE_BLUE;
  setLed(0, 0, LED_BLUE_IDLE);
  DEBUG_PRINT("[STATE] Idle (");
  DEBUG_PRINT(profileName());
  DEBUG_PRINTLN("). Press BOOT to start, hold to switch profile.");

  webPortalSetup();
}

// ---------------------------------------------------------------------------
//  Main Loop
// ---------------------------------------------------------------------------
void loop() {
  webPortalLoop();
  handleButton();
  updateLed();

  // Handle sleep/break wake-up
  if (isActive && isSleeping) {
    handleSleep();
    return;  // Don't fire actions while sleeping
  }

  // Fire an action when charged and ready
  if (isActive
      && !isSleeping
      && (ledMode == LED_ACTIVE_CHARGING || ledMode == LED_ACTIVE_READY)
      && nextActionTime != 0
      && millis() >= nextActionTime) {

    const unsigned long now = millis();

    // Enter blink state
    ledMode       = LED_ACTIVE_BLINKING;
    blinkUntil    = now + ACTION_BLINK_MS;
    ledLastToggle = now;
    ledBlinkOn    = true;
    setLed(0, LED_GREEN_CHARGE_MAX, 0);

    // Execute a weighted random action
    performJiggle();
    actionCount++;

    // Stage next interval (applied once blink ends)
    stagedInterval = random(profileIntervalMin(), profileIntervalMax());
    nextActionTime = 0;  // Prevent re-trigger during blink

    DEBUG_PRINT("[JIGGLE] Done (#");
    DEBUG_PRINT(actionCount);
    DEBUG_PRINT(" ");
    DEBUG_PRINT(profileName());
    DEBUG_PRINT("). Next in ~");
    DEBUG_PRINT(stagedInterval / 1000);
    DEBUG_PRINTLN("s");

    // Maybe take a break after this action
    maybeScheduleBreak();
  }

  // Periodic status (debug builds only)
  if (millis() - lastStatusLogTime >= STATUS_LOG_INTERVAL_MS) {
    lastStatusLogTime = millis();
    DEBUG_PRINT("[STATUS] ");
    DEBUG_PRINT(isActive ? "ACTIVE" : "IDLE");
    DEBUG_PRINT(" | profile=");
    DEBUG_PRINT(profileName());
    DEBUG_PRINT(" | led=");
    DEBUG_PRINT((int)ledMode);
    if (isActive) {
      DEBUG_PRINT(" | actions=");
      DEBUG_PRINT(actionCount);
      if (isSleeping) {
        DEBUG_PRINT(" | SLEEPING ");
        DEBUG_PRINT((sleepUntil - millis()) / 1000);
        DEBUG_PRINT("s left");
      } else if (nextActionTime > millis()) {
        DEBUG_PRINT(" | next in ");
        DEBUG_PRINT((nextActionTime - millis()) / 1000);
        DEBUG_PRINT("s");
      }
    }
    DEBUG_PRINTLN();
  }
}