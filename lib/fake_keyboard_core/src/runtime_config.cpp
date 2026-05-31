#include "runtime_config.h"

#include <Preferences.h>

#include "config.h"

namespace {
Preferences prefs;
RuntimeConfig cfg;
bool started = false;

static uint32_t clampMinMax(uint32_t v, uint32_t minV, uint32_t maxV) {
  if (v < minV) return minV;
  if (v > maxV) return maxV;
  return v;
}

static void loadOrDefaults() {
  cfg.provisioned = prefs.getBool("prov", false);
  cfg.wifiDisabled = prefs.getBool("wdis", false);

  const String ssid = prefs.getString("ssid", "");
  const String pass = prefs.getString("pass", "");
  memset(cfg.wifiSsid, 0, sizeof(cfg.wifiSsid));
  memset(cfg.wifiPass, 0, sizeof(cfg.wifiPass));
  ssid.toCharArray(cfg.wifiSsid, sizeof(cfg.wifiSsid));
  pass.toCharArray(cfg.wifiPass, sizeof(cfg.wifiPass));

  // Per-profile interval ranges
  for (uint8_t p = 0; p < PROFILE_COUNT; p++) {
    const String kMin = String("p") + String(p) + "Min";
    const String kMax = String("p") + String(p) + "Max";

    uint32_t defMin = 0;
    uint32_t defMax = 0;
    if (p == PROFILE_ACTIVE) {
      defMin = ACTIVE_INTERVAL_MIN_MS;
      defMax = ACTIVE_INTERVAL_MAX_MS;
    } else if (p == PROFILE_MEETING) {
      defMin = MEETING_INTERVAL_MIN_MS;
      defMax = MEETING_INTERVAL_MAX_MS;
    } else {
      // Reasonable fallback for any future profiles
      defMin = 10000;
      defMax = 60000;
    }

    cfg.intervalMinMs[p] = prefs.getUInt(kMin.c_str(), defMin);
    cfg.intervalMaxMs[p] = prefs.getUInt(kMax.c_str(), defMax);

    if (cfg.intervalMinMs[p] > cfg.intervalMaxMs[p]) {
      cfg.intervalMinMs[p] = defMin;
      cfg.intervalMaxMs[p] = defMax;
    }

    cfg.intervalMinMs[p] = clampMinMax(cfg.intervalMinMs[p], 250, 3600000);
    cfg.intervalMaxMs[p] = clampMinMax(cfg.intervalMaxMs[p], 250, 3600000);
  }

  cfg.actionEnabledMask = prefs.getUInt("aMask", 0xFFFFFFFFu);

  cfg.weightsConfigured = prefs.getBool("wcfg", false);
  memset(cfg.actionWeightActive, 0, sizeof(cfg.actionWeightActive));
  memset(cfg.actionWeightMeeting, 0, sizeof(cfg.actionWeightMeeting));
  if (cfg.weightsConfigured) {
    const size_t lenA = prefs.getBytesLength("wAct");
    const size_t lenM = prefs.getBytesLength("wMet");
    if (lenA >= sizeof(cfg.actionWeightActive) && lenM >= sizeof(cfg.actionWeightMeeting)) {
      prefs.getBytes("wAct", cfg.actionWeightActive, sizeof(cfg.actionWeightActive));
      prefs.getBytes("wMet", cfg.actionWeightMeeting, sizeof(cfg.actionWeightMeeting));
    } else {
      // Corrupt/old payload; fall back to defaults
      cfg.weightsConfigured = false;
    }
  }

  const String txt = prefs.getString("txt", "");
  memset(cfg.customText, 0, sizeof(cfg.customText));
  txt.toCharArray(cfg.customText, sizeof(cfg.customText));
}

}  // namespace

bool runtimeConfigBegin() {
  if (started) return true;
  started = prefs.begin("fk", false);
  if (!started) return false;
  loadOrDefaults();
  return true;
}

const RuntimeConfig& runtimeConfigGet() {
  return cfg;
}

bool runtimeConfigHasWifiCredentials() {
  return (cfg.wifiSsid[0] != '\0');
}

void runtimeConfigSetProvisioned(bool provisioned) {
  cfg.provisioned = provisioned;
  if (started) prefs.putBool("prov", provisioned);
}

bool runtimeConfigWifiDisabled() {
  return cfg.wifiDisabled;
}

void runtimeConfigSetWifiDisabled(bool disabled) {
  cfg.wifiDisabled = disabled;
  if (started) prefs.putBool("wdis", disabled);
}

void runtimeConfigSetWifiCredentials(const char* ssid, const char* pass) {
  if (!ssid) ssid = "";
  if (!pass) pass = "";

  memset(cfg.wifiSsid, 0, sizeof(cfg.wifiSsid));
  memset(cfg.wifiPass, 0, sizeof(cfg.wifiPass));
  strlcpy(cfg.wifiSsid, ssid, sizeof(cfg.wifiSsid));
  strlcpy(cfg.wifiPass, pass, sizeof(cfg.wifiPass));

  if (started) {
    prefs.putString("ssid", cfg.wifiSsid);
    prefs.putString("pass", cfg.wifiPass);
  }
}

void runtimeConfigSetProfileIntervalsMs(Profile profile, uint32_t minMs, uint32_t maxMs) {
  if (profile >= PROFILE_COUNT) return;

  minMs = clampMinMax(minMs, 250, 3600000);
  maxMs = clampMinMax(maxMs, 250, 3600000);
  if (minMs > maxMs) {
    const uint32_t t = minMs;
    minMs = maxMs;
    maxMs = t;
  }

  cfg.intervalMinMs[profile] = minMs;
  cfg.intervalMaxMs[profile] = maxMs;

  if (started) {
    const String kMin = String("p") + String((int)profile) + "Min";
    const String kMax = String("p") + String((int)profile) + "Max";
    prefs.putUInt(kMin.c_str(), minMs);
    prefs.putUInt(kMax.c_str(), maxMs);
  }
}

void runtimeConfigSetActionEnabledMask(uint32_t mask) {
  cfg.actionEnabledMask = mask;
  if (started) prefs.putUInt("aMask", mask);
}

bool runtimeConfigActionWeightsConfigured() {
  return cfg.weightsConfigured;
}

uint8_t runtimeConfigActionWeightActive(uint8_t index) {
  if (index >= 32) return 0;
  return cfg.actionWeightActive[index];
}

uint8_t runtimeConfigActionWeightMeeting(uint8_t index) {
  if (index >= 32) return 0;
  return cfg.actionWeightMeeting[index];
}

void runtimeConfigSetActionWeightsConfigured(bool configured) {
  cfg.weightsConfigured = configured;
  if (started) prefs.putBool("wcfg", configured);
}

void runtimeConfigSetActionWeight(uint8_t index, uint8_t weightActive, uint8_t weightMeeting) {
  if (index >= 32) return;

  cfg.actionWeightActive[index] = weightActive;
  cfg.actionWeightMeeting[index] = weightMeeting;
  cfg.weightsConfigured = true;

  if (started) {
    prefs.putBool("wcfg", true);
    prefs.putBytes("wAct", cfg.actionWeightActive, sizeof(cfg.actionWeightActive));
    prefs.putBytes("wMet", cfg.actionWeightMeeting, sizeof(cfg.actionWeightMeeting));
  }
}

void runtimeConfigSetCustomText(const char* text) {
  if (!text) text = "";
  memset(cfg.customText, 0, sizeof(cfg.customText));
  strlcpy(cfg.customText, text, sizeof(cfg.customText));
  if (started) prefs.putString("txt", cfg.customText);
}

const char* runtimeConfigCustomText() {
  return cfg.customText;
}

uint32_t runtimeConfigProfileIntervalMinMs(Profile profile) {
  if (profile >= PROFILE_COUNT) return 10000;
  return cfg.intervalMinMs[profile];
}

uint32_t runtimeConfigProfileIntervalMaxMs(Profile profile) {
  if (profile >= PROFILE_COUNT) return 60000;
  return cfg.intervalMaxMs[profile];
}

uint32_t runtimeConfigActionEnabledMask() {
  return cfg.actionEnabledMask;
}

void runtimeConfigFactoryReset() {
  if (!started) return;
  prefs.clear();
  loadOrDefaults();
}
