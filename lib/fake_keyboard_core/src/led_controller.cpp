#include "led_controller.h"
#include "state.h"
#include "profiles.h"
#include "device_status.h"
#include <math.h>

/// Set the onboard NeoPixel, skipping redundant writes.
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  if (r == lastR && g == lastG && b == lastB) return;
  lastR = r;
  lastG = g;
  lastB = b;
  neopixelWrite(RGB_BUILTIN, r, g, b);
}

/// Compute charge-ramp LED color given a 0.0–1.0 progress value.
void setChargeLed(float progress) {
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;

  const float curved = powf(progress, CHARGE_CURVE_GAMMA);

  const uint8_t green = LED_GREEN_CHARGE_MIN +
                        (uint8_t)((LED_GREEN_CHARGE_MAX - LED_GREEN_CHARGE_MIN) * curved);
  const uint8_t red = CHARGE_RED_START +
                      (uint8_t)((CHARGE_RED_END - CHARGE_RED_START) * curved);

  setLed(red, green, 0);
}

void ledManualBlinkGreen(unsigned long durationMs) {
  const unsigned long now = millis();
  manualBlinkUntil = now + durationMs;
  manualBlinkLastToggle = now;
  manualBlinkOn = true;
  setLed(0, LED_GREEN_CHARGE_MAX, 0);
}

void updateLed() {
  const unsigned long now = millis();

  // Manual blink overlay: provides immediate feedback (e.g., web-triggered actions)
  // without disturbing the ACTIVE scheduling state machine.
  if (manualBlinkUntil != 0 && now <= manualBlinkUntil) {
    if (now - manualBlinkLastToggle >= BLINK_TOGGLE_MS) {
      manualBlinkLastToggle = now;
      manualBlinkOn = !manualBlinkOn;
    }
    setLed(0, manualBlinkOn ? LED_GREEN_CHARGE_MAX : LED_GREEN_CHARGE_MIN, 0);
    return;
  }
  if (manualBlinkUntil != 0 && now > manualBlinkUntil) {
    manualBlinkUntil = 0;
    manualBlinkOn = false;
  }

  switch (ledMode) {

    // --- Red hold after stopping ---
    case LED_STOP_RED:
      if (now >= ledStopUntil) {
        ledMode = LED_IDLE_BLUE;
        setLed(0, 0, LED_BLUE_IDLE);
        DEBUG_PRINTLN("[LED] RED done -> BLUE idle");
      } else {
        setLed(LED_RED_STOP, 0, 0);
      }
      return;

    // --- Blue idle / boot ---
    case LED_BOOT_BLUE:
    case LED_IDLE_BLUE:
    {
      // Base color
      uint8_t r = 0, g = 0, b = LED_BLUE_IDLE;

      // Overlay a status code blink (does not affect ACTIVE modes)
      const DeviceSetupStatus st = deviceStatusGet();
      if (st != DEV_OK) {
        const unsigned long t = now % 3000UL;
        if (st == DEV_NEEDS_SETUP) {
          // Double purple blink every 3s
          const bool on = (t < 140) || (t > 260 && t < 400);
          if (on) { r = 30; g = 0; b = 40; }
        } else if (st == DEV_WIFI_CONNECTING) {
          // Amber slow blink
          const bool on = (t < 650);
          if (on) { r = 30; g = 18; b = 0; }
        } else if (st == DEV_WIFI_OFF) {
          // Single red blink
          const bool on = (t < 250);
          if (on) { r = 35; g = 0; b = 0; }
        } else if (st == DEV_WIFI_DISABLED) {
          // Triple red blink
          const bool on = (t < 120) || (t > 220 && t < 340) || (t > 440 && t < 560);
          if (on) { r = 35; g = 0; b = 0; }
        }
      } else {
        // DEV_OK: occasional green blip
        const unsigned long t = now % 8000UL;
        if (t < 120) { r = 0; g = 30; b = 0; }
      }

      setLed(r, g, b);
      return;
    }

    // --- Profile change flash ---
    case LED_PROFILE_FLASH:
      if (now >= profileFlashUntil) {
        // Return to charging
        ledMode         = LED_ACTIVE_CHARGING;
        chargeStartTime = now;
        nextInterval    = random(profileFirstMin(), profileFirstMax());
        nextActionTime  = now + nextInterval;
        setChargeLed(0.0f);
        DEBUG_PRINTLN("[LED] Profile flash done -> Charging");
      } else {
        // Purple for ACTIVE, cyan for MEETING
        if (currentProfile == PROFILE_ACTIVE)
          setLed(20, 0, 30);  // Purple
        else
          setLed(0, 20, 30);  // Cyan
      }
      return;

    // --- Sleeping / break breathing ---
    case LED_SLEEPING: {
      const float phase =
          fmodf((float)(now % LED_SLEEP_PULSE_MS) / (float)LED_SLEEP_PULSE_MS * 6.2832f, 6.2832f);
      const float breath = (sinf(phase) + 1.0f) * 0.5f;  // 0.0–1.0
      const uint8_t r     = (uint8_t)(LED_SLEEP_R * breath);
      const uint8_t g     = (uint8_t)(LED_SLEEP_G * breath);
      setLed(r, g, 0);
      return;
    }

    // --- Amber→Green charging ramp ---
    case LED_ACTIVE_CHARGING: {
      unsigned long duration = nextInterval;
      if (duration < 100) duration = 100;

      if (nextActionTime == 0) {
        chargeStartTime = now;
        nextActionTime  = now + duration;
      }

      unsigned long elapsed = now - chargeStartTime;
      if (elapsed > duration) elapsed = duration;

      const float progress = (float)elapsed / (float)duration;
      setChargeLed(progress);

      if (elapsed >= duration) {
        ledMode         = LED_ACTIVE_READY;
        readyPulseStart = now;
        setLed(0, CHARGE_READY_BRIGHTNESS, 0);
        DEBUG_PRINTLN("[LED] Charge full -> READY pulse");
      }
      return;
    }

    // --- Brief bright pulse indicating "ready to fire" ---
    case LED_ACTIVE_READY: {
      const unsigned long elapsed = now - readyPulseStart;
      if (elapsed < CHARGE_READY_PULSE_MS) {
        const float phase = (float)elapsed / (float)CHARGE_READY_PULSE_MS * 3.14159f;
        const uint8_t brightness = LED_GREEN_CHARGE_MAX +
                                  (uint8_t)((CHARGE_READY_BRIGHTNESS - LED_GREEN_CHARGE_MAX) *
                                            sinf(phase));
        setLed(0, brightness, 0);
      }
      return;
    }

    // --- Green blink during action ---
    case LED_ACTIVE_BLINKING:
      if (now > blinkUntil) {
        ledMode         = LED_ACTIVE_CHARGING;
        chargeStartTime = now;

        if (stagedInterval > 0) {
          nextInterval   = stagedInterval;
          stagedInterval = 0;
        }
        const unsigned long dur = max(100UL, nextInterval);
        nextActionTime          = now + dur;
        setChargeLed(0.0f);

        DEBUG_PRINT("[LED] Blink done -> Charging. Next in ");
        DEBUG_PRINT(dur / 1000);
        DEBUG_PRINTLN("s");
        return;
      }

      if (now - ledLastToggle >= BLINK_TOGGLE_MS) {
        ledLastToggle = now;
        ledBlinkOn    = !ledBlinkOn;
        setLed(0, ledBlinkOn ? LED_GREEN_CHARGE_MAX : LED_GREEN_CHARGE_MIN, 0);
      }
      return;
  }
}
