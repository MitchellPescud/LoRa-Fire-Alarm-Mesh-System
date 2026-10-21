#ifndef WIFI_DASHBOARD_H
#define WIFI_DASHBOARD_H

// Starts the soft AP (always on) and registers the dashboard routes.
void wifiDashboardInit();

// Services pending HTTP requests. Call every loop() pass.
void handleWebServer();

#endif
