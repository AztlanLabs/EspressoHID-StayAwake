#pragma once

#include <Arduino.h>

enum ActionSource : uint8_t {
	ACTION_SRC_AUTO = 0,
	ACTION_SRC_MANUAL = 1,
};

// Existing behavior: pick a weighted random enabled action and execute it.
void performJiggle();

// Action catalog (indices are stable within this firmware build).
int actionsCount();
const char* actionName(int index);
uint8_t actionWeightActive(int index);
uint8_t actionWeightMeeting(int index);

// Enable/disable actions (persisted via runtime_config).
uint32_t actionsEnabledMask();
bool actionEnabled(int index);
void actionsSetEnabledMask(uint32_t mask);
void actionSetEnabled(int index, bool enabled);

// Manual trigger by index (returns false if index invalid or disabled).
bool performActionByIndex(int index, ActionSource source);

// Recent action history (in-RAM ring buffer; resets on reboot).
int actionHistoryCount();
void actionHistoryToJson(String& out);
void actionHistoryClear();
