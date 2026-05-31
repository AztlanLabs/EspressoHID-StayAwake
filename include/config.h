#pragma once
// ============================================================================
//  Fake Keyboard — Configuration
//  Edit these values to tune behavior without touching main logic.
// ============================================================================

// ---------------------------------------------------------------------------
//  Firmware Version
//  Override from platformio.ini via: build_flags = -D FIRMWARE_VERSION=\"1.2.3\"
// ---------------------------------------------------------------------------
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION "0.6.0-release"
#endif

// ---------------------------------------------------------------------------
//  Hardware Pins
// ---------------------------------------------------------------------------
#define PIN_BUTTON          0       // GPIO for the toggle button (BOOT on most DevKits)

#ifndef RGB_BUILTIN
  #define RGB_BUILTIN       38      // GPIO for WS2812 RGB LED (38 or 48 depending on board)
#endif

// ---------------------------------------------------------------------------
//  USB Identity Pool  (one is chosen at random on every boot)
// ---------------------------------------------------------------------------
struct DeviceIdentity {
  uint16_t    vid;
  uint16_t    pid;
  const char* manufacturer;
  const char* product;
};

static const DeviceIdentity USB_IDENTITIES[] = {
  { 0x413C, 0x2107, "Dell",      "Dell USB Entry Keyboard"   },
  { 0x03F0, 0x034A, "HP",        "HP Elite USB Keyboard"     },
  { 0x046D, 0xC31C, "Logitech",  "USB Keyboard"              },
  { 0x045E, 0x07F8, "Microsoft", "Wired Keyboard 600"        },
  { 0x17EF, 0x608D, "Lenovo",    "ThinkPad USB Keyboard"     },
};
static constexpr int USB_IDENTITY_COUNT = sizeof(USB_IDENTITIES) / sizeof(USB_IDENTITIES[0]);

// ---------------------------------------------------------------------------
//  Button — Profile Switching
// ---------------------------------------------------------------------------
#define LONG_PRESS_MS           1500    // Hold time to cycle profiles (ms)
#define FACTORY_RESET_HOLD_MS   10000   // Hold time to factory reset + reboot (ms)

// ---------------------------------------------------------------------------
//  Profiles — Per-profile timing overrides
//  Each profile defines its own action interval range.
//  Weights are shared but the profile can scale them (see main.cpp).
// ---------------------------------------------------------------------------
enum Profile : uint8_t {
  PROFILE_ACTIVE = 0,     // Heavy keyboard work — frequent actions
  PROFILE_MEETING,        // Light, infrequent — gentle jiggles only
  PROFILE_COUNT
};

//  ACTIVE profile
#define ACTIVE_FIRST_MIN_MS         1000
#define ACTIVE_FIRST_MAX_MS         3000
#define ACTIVE_INTERVAL_MIN_MS      10000   // 10 s
#define ACTIVE_INTERVAL_MAX_MS      60000   // 60 s

//  MEETING profile
#define MEETING_FIRST_MIN_MS        3000
#define MEETING_FIRST_MAX_MS        8000
#define MEETING_INTERVAL_MIN_MS     45000   // 45 s
#define MEETING_INTERVAL_MAX_MS     180000  // 3 min

// ---------------------------------------------------------------------------
//  Sleep / Break Schedule
// ---------------------------------------------------------------------------

// Short "bathroom break" — random dormant pause mid-session
#define SLEEP_CHANCE_PER_ACTION     8       // % chance of triggering a nap after each action
#define SLEEP_MIN_MS                60000   // 1 min
#define SLEEP_MAX_MS                180000  // 3 min

// Long "lunch / end of day" break — after sustained work period
#define WORK_SESSION_MIN_MS         1200000UL   // Min work before a long break (20 min)
#define WORK_SESSION_MAX_MS         2400000UL   // Max work before a long break (40 min)
#define LONG_BREAK_MIN_MS           180000UL    // Long break min (3 min)
#define LONG_BREAK_MAX_MS           420000UL    // Long break max (7 min)

// LED colour during sleep / break (dim yellow pulse)
#define LED_SLEEP_R                 30
#define LED_SLEEP_G                 20
#define LED_SLEEP_PULSE_MS          1000    // Breathing cycle duration

// ---------------------------------------------------------------------------
//  Timing — LED Effects  (milliseconds)
// ---------------------------------------------------------------------------
#define ACTION_BLINK_MS         1000    // How long the LED blinks after an action
#define BLINK_TOGGLE_MS         100     // Blink on/off toggle speed
#define STOP_RED_HOLD_MS        2000    // Red hold duration when stopping
#define BOOT_COUNTDOWN_S        2       // Seconds to wait before initializing USB

// ---------------------------------------------------------------------------
//  Timing — Debounce
// ---------------------------------------------------------------------------
#define BUTTON_DEBOUNCE_MS      50

// ---------------------------------------------------------------------------
//  LED Brightness  (0–255, WS2812 scale — keep low, these LEDs are BRIGHT)
// ---------------------------------------------------------------------------
#define LED_BLUE_IDLE           10      // Blue while idle / boot
#define LED_RED_STOP            20      // Red while stopping
#define LED_GREEN_CHARGE_MIN    0       // "Battery empty" brightness
#define LED_GREEN_CHARGE_MAX    60      // "Battery full" / blink-on brightness

// Charge effect curve: controls how the charge ramp feels
// 1.0 = linear, 2.0 = slow start / fast finish, 0.5 = fast start / slow finish
#define CHARGE_CURVE_GAMMA      2.2f   // Exponential curve for charge ramp

// Color transition during charge:  amber (low) -> green (full)
// Set CHARGE_RED_START to 0 to keep pure green throughout
#define CHARGE_RED_START        30      // Red channel at 0% charge  (amber tint)
#define CHARGE_RED_END          0       // Red channel at 100% charge (pure green)

// "Ready" pulse: brief bright flash when charge reaches 100%
#define CHARGE_READY_PULSE_MS   400     // Duration of the "ready" pulse
#define CHARGE_READY_BRIGHTNESS 80      // Brightness during ready pulse

// Profile indicator: brief purple/cyan flash when profile changes
#define PROFILE_INDICATOR_MS    800     // How long the profile-change LED shows

// ---------------------------------------------------------------------------
//  Action Configuration — Arrow Keys
// ---------------------------------------------------------------------------
#define ARROW_PRESS_MIN         1       // Min arrow key presses per action
#define ARROW_PRESS_MAX         8       // Max arrow key presses per action (exclusive)
#define ARROW_HOLD_MIN_MS       40      // Min key-hold duration per press
#define ARROW_HOLD_MAX_MS       120     // Max key-hold duration per press
#define ARROW_GAP_MIN_MS        60      // Min gap between consecutive presses
#define ARROW_GAP_MAX_MS        500     // Max gap between consecutive presses
#define ARROW_REVERSE_CHANCE    30      // % chance of reversing direction mid-scroll
#define ARROW_REVERSE_RATIO     3       // Reverse presses = total / this value

// ---------------------------------------------------------------------------
//  Action Configuration — Typing
// ---------------------------------------------------------------------------
#define TYPE_WPM_MIN            60      // Minimum simulated WPM for key delays
#define TYPE_WPM_MAX            120     // Maximum simulated WPM for key delays

// ---------------------------------------------------------------------------
//  Action Weights  (higher = more likely to be chosen)
//  In MEETING profile, aggressive actions (AltTab, WinArrow) are zeroed.
// ---------------------------------------------------------------------------
#define WEIGHT_ARROW_SCROLL     35
#define WEIGHT_ALT_TAB          10
#define WEIGHT_VOLUME           3
#define WEIGHT_BRIGHTNESS       3
#define WEIGHT_CAPS_TOGGLE      5
#define WEIGHT_NUMLOCK_TOGGLE   5
#define WEIGHT_SHIFT_TAP        15
#define WEIGHT_WIN_ARROW        10

// ---------------------------------------------------------------------------
//  Serial Debug  (enable via platformio.ini: build_flags = -D DEBUG_MODE)
// ---------------------------------------------------------------------------
#ifdef DEBUG_MODE
  #define DEBUG_BEGIN(...)   Serial.begin(__VA_ARGS__)
  #define DEBUG_PRINT(...)   Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_BEGIN(...)
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
#endif

// ---------------------------------------------------------------------------
//  Status Log Interval
// ---------------------------------------------------------------------------
#define STATUS_LOG_INTERVAL_MS  10000   // How often to print status (debug only)
