#pragma once

// Starts Wi-Fi + web server.
// - If Wi-Fi credentials are missing (or STA connect fails), starts a SoftAP captive portal.
// - Otherwise serves the companion webapp + JSON API on the STA IP.
void webPortalSetup();

// Call from loop() to process DNS (captive portal) and HTTP requests.
void webPortalLoop();
