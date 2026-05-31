#include "control.h"

#include "state.h"
#include "profiles.h"
#include "led_controller.h"
#include "sleep_manager.h"
#include "event_log.h"

#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

extern USBHIDKeyboard        Keyboard;
extern USBHIDConsumerControl Consumer;

void controlSetActive(bool active) {
  const unsigned long now = millis();
  if (isActive == active) return;

  isActive = active;

  if (isActive) {
    actionCount      = 0;
    sessionStartTime = now;
    isSleeping       = false;
    ledMode          = LED_ACTIVE_CHARGING;
    chargeStartTime  = now;
    nextInterval     = random(profileFirstMin(), profileFirstMax());
    stagedInterval   = 0;
    nextActionTime   = chargeStartTime + nextInterval;
    scheduleNextLongBreak();
    setChargeLed(0.0f);

    DEBUG_PRINT("[STATE] ACTIVE (");
    DEBUG_PRINT(profileName());
    DEBUG_PRINTLN(")");

    eventLogAdd(String("ACTIVE (") + profileName() + ")");
  } else {
    Keyboard.releaseAll();
    Consumer.release();
    isSleeping     = false;
    ledMode        = LED_STOP_RED;
    ledStopUntil   = now + STOP_RED_HOLD_MS;
    nextActionTime = 0;
    stagedInterval = 0;
    setLed(LED_RED_STOP, 0, 0);

    DEBUG_PRINT("[STATE] STOPPED after ");
    DEBUG_PRINT(actionCount);
    DEBUG_PRINTLN(" actions");

    eventLogAdd(String("STOPPED after ") + String(actionCount) + " actions");
  }
}

void controlToggleActive() {
  controlSetActive(!isActive);
}

void controlSetProfile(Profile profile) {
  if (profile >= PROFILE_COUNT) return;
  if (currentProfile == profile) return;

  currentProfile = profile;

  DEBUG_PRINT("[PROFILE] Switched to: ");
  DEBUG_PRINTLN(profileName());

  eventLogAdd(String("Profile -> ") + profileName());

  const unsigned long now = millis();
  if (isActive) {
    ledMode           = LED_PROFILE_FLASH;
    profileFlashUntil = now + PROFILE_INDICATOR_MS;

    if (isSleeping) {
      isSleeping = false;
      DEBUG_PRINTLN("[SLEEP] Woke up due to profile change");
    }
  }
}

void controlSleepNow(unsigned long durationMs, const char* reason) {
  if (!isActive) return;
  enterSleep(durationMs, reason ? reason : "Sleep");
  eventLogAdd(String("Sleep ") + String(durationMs / 1000) + "s");
}

void controlWakeNow() {
  if (!isSleeping) return;
  // Wake immediately
  isSleeping      = false;
  ledMode         = LED_ACTIVE_CHARGING;
  chargeStartTime = millis();
  nextInterval    = random(profileFirstMin(), profileFirstMax());
  nextActionTime  = chargeStartTime + nextInterval;
  setChargeLed(0.0f);

  DEBUG_PRINTLN("[SLEEP] Forced wake — resuming work");
  eventLogAdd("Wake");
}

void controlReboot() {
  delay(100);
  ESP.restart();
}
