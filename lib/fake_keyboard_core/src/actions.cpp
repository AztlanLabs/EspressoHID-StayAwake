#include "actions.h"
#include "led_controller.h"
#include "state.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"
#include "runtime_config.h"
#include "event_log.h"
#include "human_input.h"

// ---------------------------------------------------------------------------
//  Action History (in-RAM)
// ---------------------------------------------------------------------------
namespace {

struct ActionHistoryEntry {
  uint32_t atMs;
  const char* name;
  ActionSource source;
};

static constexpr uint8_t ACTION_HISTORY_MAX = 20;
static ActionHistoryEntry history[ACTION_HISTORY_MAX] = {};
static uint8_t historyHead = 0;   // next write position
static uint8_t historyCount_ = 0; // number of valid items

static void logAction(const char* name, ActionSource source) {
  history[historyHead] = {millis(), name, source};
  historyHead = (uint8_t)((historyHead + 1) % ACTION_HISTORY_MAX);
  if (historyCount_ < ACTION_HISTORY_MAX) historyCount_++;

  eventLogAdd(String(source == ACTION_SRC_MANUAL ? "ACT(manual): " : "ACT(auto): ") + (name ? name : ""));
}

static bool actionEnabledByMask(int idx) {
  if (idx < 0) return false;
  if (idx >= 32) return true; // outside mask range; treat as enabled
  return (runtimeConfigActionEnabledMask() & (1u << (uint32_t)idx)) != 0;
}

}  // namespace

// ---------------------------------------------------------------------------
//  HID Instances
// ---------------------------------------------------------------------------
extern USBHIDKeyboard        Keyboard;
extern USBHIDConsumerControl Consumer;

// ---------------------------------------------------------------------------
//  Utility — Human-like Delay
// ---------------------------------------------------------------------------

// NOTE: human-like timing helpers live in human_input.{h,cpp}

// ---------------------------------------------------------------------------
//  Actions  (each function = one self-contained keyboard-only action)
// ---------------------------------------------------------------------------

// --- Alt+Tab: switch to next window, pause, then Alt+Shift+Tab back ---
static void actionAltTab() {
  DEBUG_PRINTLN("[ACT] Alt+Tab (round-trip)");

  // Switch to next window
  Keyboard.press(KEY_LEFT_ALT);
  delay(humanDelayMs(TYPE_WPM_MIN, TYPE_WPM_MAX));
  Keyboard.press(KEY_TAB);
  delay(50);
  Keyboard.releaseAll();

  // Dwell on the other window (like glancing at it)
  delay(random(500, 1500));

  // Return: Alt+Shift+Tab goes back to the previous window
  Keyboard.press(KEY_LEFT_ALT);
  delay(humanDelayMs(TYPE_WPM_MIN, TYPE_WPM_MAX));
  Keyboard.press(KEY_LEFT_SHIFT);
  delay(30);
  Keyboard.press(KEY_TAB);
  delay(50);
  Keyboard.releaseAll();

  // Extra safety against sticky modifiers
  delay(20);
  Keyboard.releaseAll();
}

// --- Arrow scroll: natural scrolling with occasional direction reversal ---
static void actionArrowScroll() {
  // Net-zero scroll: press one direction N times, then the opposite direction N times.
  static const uint8_t verticalKeys[]   = {KEY_UP_ARROW, KEY_DOWN_ARROW};
  static const uint8_t horizontalKeys[] = {KEY_LEFT_ARROW, KEY_RIGHT_ARROW};

  const bool vertical = (random(100) < 80);
  const uint8_t* pair = vertical ? verticalKeys : horizontalKeys;

  const uint8_t downKey = pair[1];
  const uint8_t upKey   = pair[0];

  const int presses = random(ARROW_PRESS_MIN, ARROW_PRESS_MAX);

  DEBUG_PRINT("[ACT] Arrow scroll net-zero x");
  DEBUG_PRINT(presses);
  DEBUG_PRINTLN(vertical ? " (vertical)" : " (horizontal)");

  const bool startDown = (random(100) < 50);
  const uint8_t first = startDown ? downKey : upKey;
  const uint8_t second = startDown ? upKey : downKey;

  for (int i = 0; i < presses; i++) {
    tapKey(Keyboard, first, ARROW_HOLD_MIN_MS, ARROW_HOLD_MAX_MS, ARROW_GAP_MIN_MS, ARROW_GAP_MAX_MS);
  }

  delay(random(220, 900));

  for (int i = 0; i < presses; i++) {
    tapKey(Keyboard, second, ARROW_HOLD_MIN_MS, ARROW_HOLD_MAX_MS, ARROW_GAP_MIN_MS, ARROW_GAP_MAX_MS);
  }
}

// --- Volume nudge: +1 then -1, net zero ---
static void actionVolume() {
  DEBUG_PRINTLN("[ACT] Volume +/-");
  tapConsumer(Consumer, CONSUMER_CONTROL_VOLUME_INCREMENT, 35, 85);
  delay(random(80, 200));
  tapConsumer(Consumer, CONSUMER_CONTROL_VOLUME_DECREMENT, 35, 85);
}

// --- Brightness nudge: +1 then -1, net zero ---
static void actionBrightness() {
  DEBUG_PRINTLN("[ACT] Brightness +/-");
  tapConsumer(Consumer, CONSUMER_CONTROL_BRIGHTNESS_INCREMENT, 35, 85);
  delay(random(80, 200));
  tapConsumer(Consumer, CONSUMER_CONTROL_BRIGHTNESS_DECREMENT, 35, 85);
}

// --- Caps Lock toggle: press twice (on then off), net zero ---
static void actionCapsToggle() {
  DEBUG_PRINTLN("[ACT] Caps Lock toggle");
  tapKey(Keyboard, KEY_CAPS_LOCK, 45, 95, 120, 260);
  delay(random(150, 400));
  tapKey(Keyboard, KEY_CAPS_LOCK, 45, 95, 60, 180);
}

// --- Num Lock toggle: press twice (on then off), net zero ---
static void actionNumLockToggle() {
  DEBUG_PRINTLN("[ACT] Num Lock toggle");
  tapKey(Keyboard, 0xDB, 45, 95, 160, 260);
  delay(random(200, 500));
  tapKey(Keyboard, 0xDB, 45, 95, 60, 180);
}

// --- Shift tap: press and release Shift briefly (completely invisible) ---
static void actionShiftTap() {
  DEBUG_PRINTLN("[ACT] Shift tap");
  tapKey(Keyboard, KEY_LEFT_SHIFT, 25, 70, 60, 160);
}

// --- Window move: Win+Arrow in one direction, then Win+Arrow back (net zero) ---
static void actionWinArrow() {
  // Previous Win+Arrow tiling is not reliably reversible across OS/window states.
  // Replace with a Win-menu open/close (net-zero, deterministic).
  DEBUG_PRINTLN("[ACT] Win key (open/close)");
  tapKey(Keyboard, KEY_LEFT_GUI, 35, 80, 180, 300);
  delay(random(250, 700));
  tapKey(Keyboard, KEY_LEFT_GUI, 35, 80, 60, 180);
}

static void actionTypeText() {
  const char* txt = runtimeConfigCustomText();
  if (!txt || txt[0] == '\0') {
    DEBUG_PRINTLN("[ACT] TypeText skipped (empty)");
    return;
  }

  DEBUG_PRINTLN("[ACT] TypeText (net-zero)");
  // Type, pause, then erase with backspace.
  const size_t len = strlen(txt);
  typeTextHuman(Keyboard, txt, TYPE_WPM_MIN, TYPE_WPM_MAX);
  delay(random(120, 420));
  backspaceHuman(Keyboard, len);
}

static void actionCtrlTap() {
  DEBUG_PRINTLN("[ACT] Ctrl tap");
  tapKey(Keyboard, KEY_LEFT_CTRL, 25, 70, 60, 160);
}

static void actionWinSearchPeek() {
  // Windows: Win+S opens Search. ESC closes it.
  DEBUG_PRINTLN("[ACT] Win+S (peek)");
  const uint8_t keys[] = {KEY_LEFT_GUI, HID_KEY_S};
  chordKey(Keyboard, keys, 2, 20, 60, 40, 120);
  delay(random(220, 700));
  tapKey(Keyboard, KEY_ESC, 25, 70, 60, 160);
}

static void actionEmojiPeek() {
  // Windows: Win+. opens emoji panel. ESC closes it.
  DEBUG_PRINTLN("[ACT] Win+. (peek)");
  const uint8_t keys[] = {KEY_LEFT_GUI, HID_KEY_PERIOD};
  chordKey(Keyboard, keys, 2, 20, 60, 40, 120);
  delay(random(220, 700));
  tapKey(Keyboard, KEY_ESC, 25, 70, 60, 160);
}

// ---------------------------------------------------------------------------
//  Weighted Action Selection
// ---------------------------------------------------------------------------
struct WeightedAction {
  void (*fn)();
  uint8_t weightActive;   // Weight in ACTIVE profile
  uint8_t weightMeeting;  // Weight in MEETING profile
  const char* name;
};

static const WeightedAction ACTIONS[] = {
  //                      fn              ACTIVE                 MEETING              name
  {actionArrowScroll,   WEIGHT_ARROW_SCROLL, WEIGHT_ARROW_SCROLL, "ArrowScroll"},
  {actionAltTab,        WEIGHT_ALT_TAB,      0,                   "AltTab"},
  {actionVolume,        WEIGHT_VOLUME,       WEIGHT_VOLUME,       "Volume"},
  {actionBrightness,    WEIGHT_BRIGHTNESS,   WEIGHT_BRIGHTNESS,   "Brightness"},
  {actionCapsToggle,    WEIGHT_CAPS_TOGGLE,  WEIGHT_CAPS_TOGGLE,  "CapsToggle"},
  {actionNumLockToggle, WEIGHT_NUMLOCK_TOGGLE, WEIGHT_NUMLOCK_TOGGLE, "NumLockToggle"},
  {actionShiftTap,      WEIGHT_SHIFT_TAP,    WEIGHT_SHIFT_TAP * 2, "ShiftTap"},
  {actionWinArrow,      WEIGHT_WIN_ARROW,    0,                   "WinArrow"},
  // Optional: only meaningful when runtime_config customText is set
  {actionTypeText,      2,                   0,                   "TypeText"},
  {actionCtrlTap,       10,                  10,                  "CtrlTap"},
  {actionWinSearchPeek, 3,                   0,                   "WinSearch"},
  {actionEmojiPeek,     2,                   0,                   "EmojiPeek"},
};
static constexpr int ACTION_COUNT = sizeof(ACTIONS) / sizeof(ACTIONS[0]);

int actionsCount() {
  return ACTION_COUNT;
}

const char* actionName(int index) {
  if (index < 0 || index >= ACTION_COUNT) return "";
  return ACTIONS[index].name;
}

uint8_t actionWeightActive(int index) {
  if (index < 0 || index >= ACTION_COUNT) return 0;
  if (runtimeConfigActionWeightsConfigured() && index < 32) {
    return runtimeConfigActionWeightActive((uint8_t)index);
  }
  return ACTIONS[index].weightActive;
}

uint8_t actionWeightMeeting(int index) {
  if (index < 0 || index >= ACTION_COUNT) return 0;
  if (runtimeConfigActionWeightsConfigured() && index < 32) {
    return runtimeConfigActionWeightMeeting((uint8_t)index);
  }
  return ACTIONS[index].weightMeeting;
}

uint32_t actionsEnabledMask() {
  return runtimeConfigActionEnabledMask();
}

bool actionEnabled(int index) {
  if (index < 0 || index >= ACTION_COUNT) return false;
  return actionEnabledByMask(index);
}

void actionsSetEnabledMask(uint32_t mask) {
  runtimeConfigSetActionEnabledMask(mask);
}

void actionSetEnabled(int index, bool enabled) {
  if (index < 0 || index >= ACTION_COUNT) return;
  uint32_t mask = runtimeConfigActionEnabledMask();
  const uint32_t bit = (index < 32) ? (1u << (uint32_t)index) : 0u;
  if (bit == 0u) return;
  mask = enabled ? (mask | bit) : (mask & ~bit);
  runtimeConfigSetActionEnabledMask(mask);
}

bool performActionByIndex(int index, ActionSource source) {
  if (index < 0 || index >= ACTION_COUNT) return false;
  if (!actionEnabledByMask(index)) return false;
  if (source == ACTION_SRC_MANUAL) {
    ledManualBlinkGreen(ACTION_BLINK_MS);
  }
  ACTIONS[index].fn();
  logAction(ACTIONS[index].name, source);
  return true;
}

int actionHistoryCount() {
  return historyCount_;
}

void actionHistoryToJson(String& out) {
  out = "[";
  for (uint8_t i = 0; i < historyCount_; i++) {
    const int idx = (int)((historyHead + ACTION_HISTORY_MAX - historyCount_ + i) % ACTION_HISTORY_MAX);
    const ActionHistoryEntry& e = history[idx];
    if (i) out += ',';
    out += "{\"ms\":";
    out += String(e.atMs);
    out += ",\"name\":\"";
    out += e.name ? e.name : "";
    out += "\",\"src\":";
    out += String((int)e.source);
    out += '}';
  }
  out += "]";
}

void actionHistoryClear() {
  for (uint8_t i = 0; i < ACTION_HISTORY_MAX; i++) {
    history[i] = {0, nullptr, ACTION_SRC_AUTO};
  }
  historyHead = 0;
  historyCount_ = 0;
}

/// Pick a random action using profile-specific weights.
void performJiggle() {
  int totalWeight = 0;
  for (int i = 0; i < ACTION_COUNT; i++) {
    if (!actionEnabledByMask(i)) continue;
    uint8_t w = (currentProfile == PROFILE_MEETING) ? actionWeightMeeting(i)
                                                    : actionWeightActive(i);
    if (strcmp(ACTIONS[i].name, "TypeText") == 0) {
      const char* t = runtimeConfigCustomText();
      if (!t || t[0] == '\0') w = 0;
    }
    totalWeight += w;
  }

  if (totalWeight == 0) {
    // Safety: if all weights are 0, just do a shift tap
    actionShiftTap();
    logAction("ShiftTap", ACTION_SRC_AUTO);
    return;
  }

  int roll = random(0, totalWeight);
  for (int i = 0; i < ACTION_COUNT; i++) {
    if (!actionEnabledByMask(i)) continue;
    uint8_t w = (currentProfile == PROFILE_MEETING) ? actionWeightMeeting(i)
                                                    : actionWeightActive(i);
    if (strcmp(ACTIONS[i].name, "TypeText") == 0) {
      const char* t = runtimeConfigCustomText();
      if (!t || t[0] == '\0') w = 0;
    }
    roll -= w;
    if (roll < 0) {
      DEBUG_PRINT("[JIGGLE] Selected: ");
      DEBUG_PRINTLN(ACTIONS[i].name);
      ACTIONS[i].fn();
      logAction(ACTIONS[i].name, ACTION_SRC_AUTO);
      return;
    }
  }

  // Fallback
  ACTIONS[0].fn();
  logAction(ACTIONS[0].name, ACTION_SRC_AUTO);
}
