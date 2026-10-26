#ifndef WIFI_DASHBOARD_H
#define WIFI_DASHBOARD_H

// Registers HTTP routes. Does NOT start the AP itself - the AP only comes
// up once the WiFi toggle switch (PIN_WIFI_SWITCH) is turned on, matching
// the switch's documented hardware purpose.
void wifiDashboardInit();

// Reads the WiFi toggle switch, drives the WiFi status LED, and starts/stops
// the soft AP + web server on a switch state change (edge-triggered, not
// every loop() pass).
void handleWifiSwitch();

// Services pending HTTP requests. No-op while the AP is off.
void handleWebServer();

#endif
