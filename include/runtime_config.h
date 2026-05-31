#pragma once

#include <Arduino.h>
#include "config.h"

// Persistent configuration stored in ESP32 NVS.
// Defaults come from include/config.h but values can be overridden at runtime.

struct RuntimeConfig {
  bool provisioned = false;
  bool wifiDisabled = false;

  // Wi-Fi credentials (STA)
  char wifiSsid[33] = {0};
  char wifiPass[65] = {0};

  // Interval ranges per profile (ms)
  uint32_t intervalMinMs[PROFILE_COUNT] = {};
  uint32_t intervalMaxMs[PROFILE_COUNT] = {};

  // Bit i corresponds to action index i in actions.cpp
  uint32_t actionEnabledMask = 0xFFFFFFFFu;

  // Optional runtime weight overrides.
  // When weightsConfigured=false, firmware uses compile-time defaults from actions.cpp.
  bool weightsConfigured = false;
  uint8_t actionWeightActive[32] = {};
  uint8_t actionWeightMeeting[32] = {};

  // Optional custom text for the TypeText action
  char customText[129] = {0};
};

// Load config from NVS (or defaults on first run). Call once in setup().
bool runtimeConfigBegin();

// Access the in-RAM config snapshot.
const RuntimeConfig& runtimeConfigGet();

// Convenience helpers
bool runtimeConfigHasWifiCredentials();

// Mutators persist immediately to NVS.
void runtimeConfigSetProvisioned(bool provisioned);
bool runtimeConfigWifiDisabled();
void runtimeConfigSetWifiDisabled(bool disabled);
void runtimeConfigSetWifiCredentials(const char* ssid, const char* pass);
void runtimeConfigSetProfileIntervalsMs(Profile profile, uint32_t minMs, uint32_t maxMs);
void runtimeConfigSetActionEnabledMask(uint32_t mask);

// Action weights (optional override; indices beyond 31 are ignored)
bool runtimeConfigActionWeightsConfigured();
uint8_t runtimeConfigActionWeightActive(uint8_t index);
uint8_t runtimeConfigActionWeightMeeting(uint8_t index);
void runtimeConfigSetActionWeightsConfigured(bool configured);
void runtimeConfigSetActionWeight(uint8_t index, uint8_t weightActive, uint8_t weightMeeting);

void runtimeConfigSetCustomText(const char* text);
const char* runtimeConfigCustomText();

// Clears NVS namespace and resets to defaults (requires reboot to take effect everywhere).
void runtimeConfigFactoryReset();

// Read helpers (fall back to defaults if unset)
uint32_t runtimeConfigProfileIntervalMinMs(Profile profile);
uint32_t runtimeConfigProfileIntervalMaxMs(Profile profile);
uint32_t runtimeConfigActionEnabledMask();
