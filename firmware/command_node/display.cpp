#include <Arduino.h>
#include <WiFi.h>
#include "display.h"
#include "config.h"
#include "state.h"
#include "mesh.h"

static unsigned long lastDisplayMs = 0;

void displayInit() {
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.drawString(0, 0, "Fire Command");
  display.drawString(0, 12, "Starting...");
  display.display();
}

void handleDisplay() {
  unsigned long now = millis();
  if (now - lastDisplayMs < DISPLAY_REFRESH_MS) return;
  lastDisplayMs = now;

  display.clear();
  display.setFont(ArialMT_Plain_16);
  if (fireActive) {
    int n = meshActiveAlarmCount();
    display.drawString(0, 0, n > 1 ? ("FIRE x" + String(n)) : ("FIRE node " + String(meshFirstAlarmOrigin())));
  } else {
    display.drawString(0, 0, "All clear");
  }

  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 20, "Nodes heard: " + String(meshPeerCount()));
  display.drawString(0, 34, String("WiFi: ") + WIFI_AP_SSID);
  display.drawString(0, 48, "http://" + WiFi.softAPIP().toString());
  display.display();
}
