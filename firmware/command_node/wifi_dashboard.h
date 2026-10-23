#ifndef WIFI_DASHBOARD_H
#define WIFI_DASHBOARD_H

#include <Arduino.h>

// Registers the dashboard routes and loads the saved network. The WiFi mode
// itself is chosen by the WiFi switch (see handleWifiSwitch), so nothing is
// started here.
void wifiDashboardInit();

// Call every loop() pass. Reads the WiFi switch (GPIO16, ON = grounded):
//   ON  -> own access point (WIFI_AP_SSID)
//   OFF -> station mode: connects to the saved network, retrying while it is unreachable
// Also keeps the station connection alive and logs connect / disconnect.
void handleWifiSwitch();

// Services pending HTTP requests. Call every loop() pass.
void handleWebServer();

// ---- saved network (station mode), managed from the serial terminal ----
// ssid 1..32 chars; password empty (open network) or 8..63 chars. Saved to flash.
// If currently in station mode the node reconnects immediately.
bool wifiSaveCredentials(const String &ssid, const String &password);
// Erases the saved network and falls back to WIFI_STA_DEFAULT_* from config.h.
void wifiResetCredentials();
void wifiPrintStatus();

// Two short lines for the OLED, e.g. "WiFi: FireCommand" / "http://192.168.4.1".
void wifiDisplayLines(String &line1, String &line2);

#endif
