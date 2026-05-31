#ifndef PROFILES_H
#define PROFILES_H

#include <Arduino.h>
#include "config.h"

void scheduleNextLongBreak();
const char* profileName();
const char* profileName(Profile profile);
unsigned long profileFirstMin();
unsigned long profileFirstMax();
unsigned long profileIntervalMin();
unsigned long profileIntervalMax();

unsigned long profileIntervalMin(Profile profile);
unsigned long profileIntervalMax(Profile profile);

#endif // PROFILES_H
