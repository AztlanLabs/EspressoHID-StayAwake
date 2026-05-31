#include "usb_identity.h"
#include "state.h"
#include "USB.h"

// ---------------------------------------------------------------------------
//  USB Identity Setup
// ---------------------------------------------------------------------------
void applyRandomIdentity() {
  const uint32_t seed = analogRead(1) ^ (micros() * 2654435761UL) ^ esp_random();
  randomSeed(seed);

  const int idx = random(0, USB_IDENTITY_COUNT);
  USB.VID(USB_IDENTITIES[idx].vid);
  USB.PID(USB_IDENTITIES[idx].pid);
  USB.manufacturerName(USB_IDENTITIES[idx].manufacturer);
  USB.productName(USB_IDENTITIES[idx].product);

  char serial[20];
  snprintf(serial, sizeof(serial), "%08lX%08lX", (unsigned long)random(0x7FFFFFFFL),
           (unsigned long)random(0x7FFFFFFFL));
  USB.serialNumber(serial);

  DEBUG_PRINT("[USB] ");
  DEBUG_PRINT(USB_IDENTITIES[idx].manufacturer);
  DEBUG_PRINT(" / ");
  DEBUG_PRINTLN(USB_IDENTITIES[idx].product);
}
