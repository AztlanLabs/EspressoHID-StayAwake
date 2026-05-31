#pragma once

#include <Arduino.h>

// Small in-RAM event log for the web UI (resets on reboot).

void eventLogAdd(const String& msg);
void eventLogToJson(String& out);
void eventLogClear();
