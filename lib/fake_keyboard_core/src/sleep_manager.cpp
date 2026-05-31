#include "sleep_manager.h"
#include "state.h"
#include "profiles.h"
#include "led_controller.h"
#include "event_log.h"

/// Enter a dormant sleep state for the given duration.
void enterSleep(unsigned long durationMs, const char* reason) {
  isSleeping    = true;
  sleepUntil    = millis() + durationMs;
  ledMode       = LED_SLEEPING;
  nextActionTime = 0;

  DEBUG_PRINT("[SLEEP] ");
  DEBUG_PRINT(reason);
  DEBUG_PRINT(" for ");
  DEBUG_PRINT(durationMs / 1000);
  DEBUG_PRINTLN("s");

  eventLogAdd(String("SLEEP: ") + (reason ? reason : "") + " " + String(durationMs / 1000) + "s");
}

/// Check if it's time to wake up from sleep.
void handleSleep() {
  if (!isSleeping) return;
  if (millis() < sleepUntil) return;

  // Wake up
  isSleeping      = false;
  ledMode         = LED_ACTIVE_CHARGING;
  chargeStartTime = millis();
  nextInterval    = random(profileFirstMin(), profileFirstMax());
  nextActionTime  = chargeStartTime + nextInterval;
  setChargeLed(0.0f);

  DEBUG_PRINTLN("[SLEEP] Woke up — resuming work");
  eventLogAdd("SLEEP: woke up");
}

/// Decide whether to take a short nap or a long break.
/// Called after each action completes.
void maybeScheduleBreak() {
  const unsigned long now = millis();

  // Long break: sustained work session exceeded
  if (nextLongBreakAt > 0 && now >= nextLongBreakAt) {
    const unsigned long breakDur = random(LONG_BREAK_MIN_MS, LONG_BREAK_MAX_MS);
    enterSleep(breakDur, "Long break (work session)");
    scheduleNextLongBreak();  // Schedule next one for after we resume
    return;
  }

  // Short nap: random chance per action
  if (random(100) < SLEEP_CHANCE_PER_ACTION) {
    const unsigned long napDur = random(SLEEP_MIN_MS, SLEEP_MAX_MS);
    enterSleep(napDur, "Short nap (bathroom break)");
  }
}
