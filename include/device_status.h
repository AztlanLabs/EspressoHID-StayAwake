#pragma once

#include <Arduino.h>

// High-level device status for LED signaling.
// Kept Wi-Fi/framework agnostic: web_portal updates it, led_controller reads it.

enum DeviceSetupStatus : uint8_t {
  DEV_OK = 0,            // Provisioned + normal operation
  DEV_NEEDS_SETUP = 1,   // No credentials / AP portal
  DEV_WIFI_CONNECTING = 2,
  DEV_WIFI_OFF = 3,      // Wi-Fi suppressed this boot
  DEV_WIFI_DISABLED = 4, // Wi-Fi permanently disabled via setup
};

void deviceStatusSet(DeviceSetupStatus status);
DeviceSetupStatus deviceStatusGet();
