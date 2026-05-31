#include "profiles.h"
#include "state.h"
#include "runtime_config.h"

/// Return the first-action delay range for the current profile.
unsigned long profileFirstMin() {
  return (currentProfile == PROFILE_MEETING) ? MEETING_FIRST_MIN_MS : ACTIVE_FIRST_MIN_MS;
}
unsigned long profileFirstMax() {
  return (currentProfile == PROFILE_MEETING) ? MEETING_FIRST_MAX_MS : ACTIVE_FIRST_MAX_MS;
}

/// Return the inter-action interval range for the current profile.
unsigned long profileIntervalMin() {
  return profileIntervalMin(currentProfile);
}
unsigned long profileIntervalMax() {
  return profileIntervalMax(currentProfile);
}

/// Return the name of the current profile.
const char* profileName() {
  return profileName(currentProfile);
}

const char* profileName(Profile profile) {
  if (profile == PROFILE_MEETING) return "MEETING";
  if (profile == PROFILE_ACTIVE) return "ACTIVE";
  static char buf[16];
  snprintf(buf, sizeof(buf), "PROFILE_%d", (int)profile);
  return buf;
}

unsigned long profileIntervalMin(Profile profile) {
  return runtimeConfigProfileIntervalMinMs(profile);
}

unsigned long profileIntervalMax(Profile profile) {
  return runtimeConfigProfileIntervalMaxMs(profile);
}

/// Schedule the next long break timestamp.
void scheduleNextLongBreak() {
  nextLongBreakAt = millis() + random(WORK_SESSION_MIN_MS, WORK_SESSION_MAX_MS);
}
