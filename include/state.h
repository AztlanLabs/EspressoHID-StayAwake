#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include "config.h"

// ---------------------------------------------------------------------------
//  State Machine Enums
// ---------------------------------------------------------------------------
enum LedMode : uint8_t {
  LED_BOOT_BLUE,          // Initial boot-up blue
  LED_IDLE_BLUE,          // Waiting for button press
  LED_ACTIVE_CHARGING,    // Amber→green ramp while waiting for next action
  LED_ACTIVE_READY,       // Brief bright pulse when charge is full
  LED_ACTIVE_BLINKING,    // Fast green blink during / right after an action
  LED_STOP_RED,           // Red flash when deactivated
  LED_SLEEPING,           // Dim yellow breathing while dormant
  LED_PROFILE_FLASH       // Brief color flash on profile change
};

// ---------------------------------------------------------------------------
//  Global State Variables
// ---------------------------------------------------------------------------
extern bool          isActive;
extern LedMode       ledMode;
extern Profile       currentProfile;
extern unsigned long chargeStartTime;
extern unsigned long nextActionTime;
extern unsigned long nextInterval;
extern unsigned long stagedInterval;
extern unsigned long ledLastToggle;
extern unsigned long blinkUntil;
extern unsigned long ledStopUntil;
extern unsigned long readyPulseStart;
extern unsigned long profileFlashUntil;
extern bool          ledBlinkOn;
extern unsigned long manualBlinkUntil;
extern unsigned long manualBlinkLastToggle;
extern bool          manualBlinkOn;
extern uint8_t       lastR;
extern uint8_t       lastG;
extern uint8_t       lastB;
extern unsigned long actionCount;
extern bool          isSleeping;
extern unsigned long sleepUntil;
extern unsigned long sessionStartTime;
extern unsigned long nextLongBreakAt;
extern unsigned long lastStatusLogTime;

#endif // STATE_H
