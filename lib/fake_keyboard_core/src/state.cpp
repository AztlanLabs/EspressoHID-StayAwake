#include "state.h"

// ---------------------------------------------------------------------------
//  Global State Variable Definitions
// ---------------------------------------------------------------------------
bool isActive                  = false;
LedMode ledMode                = LED_BOOT_BLUE;
Profile currentProfile         = PROFILE_ACTIVE;
unsigned long chargeStartTime  = 0;
unsigned long nextActionTime   = 0;
unsigned long nextInterval     = 0;
unsigned long stagedInterval   = 0;
unsigned long ledLastToggle    = 0;
unsigned long blinkUntil       = 0;
unsigned long ledStopUntil     = 0;
unsigned long readyPulseStart  = 0;
unsigned long profileFlashUntil = 0;
bool ledBlinkOn                = false;
unsigned long manualBlinkUntil = 0;
unsigned long manualBlinkLastToggle = 0;
bool manualBlinkOn             = false;
uint8_t lastR                  = 255;
uint8_t lastG                  = 255;
uint8_t lastB                  = 255;
unsigned long actionCount      = 0;
bool isSleeping                = false;
unsigned long sleepUntil       = 0;
unsigned long sessionStartTime = 0;
unsigned long nextLongBreakAt  = 0;
unsigned long lastStatusLogTime = 0;
