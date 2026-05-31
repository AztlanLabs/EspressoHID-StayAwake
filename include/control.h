#pragma once

#include <Arduino.h>
#include "config.h"

// Centralized state transitions used by both the button and the web UI.

void controlSetActive(bool active);
void controlToggleActive();

void controlSetProfile(Profile profile);

// Sleep/wake controls
void controlSleepNow(unsigned long durationMs, const char* reason);
void controlWakeNow();

// Reboot
void controlReboot();
