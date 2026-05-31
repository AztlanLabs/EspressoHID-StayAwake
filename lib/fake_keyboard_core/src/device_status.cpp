#include "device_status.h"

namespace {
static volatile DeviceSetupStatus g_status = DEV_OK;
}

void deviceStatusSet(DeviceSetupStatus status) {
  g_status = status;
}

DeviceSetupStatus deviceStatusGet() {
  return g_status;
}
