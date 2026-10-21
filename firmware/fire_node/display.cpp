#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "state.h"
#include "battery.h"

static unsigned long lastDisplayMs = 0;

void displayInit() {
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.drawString(0, 0, "Fire node");
  display.drawString(0, 12, "Initializing...");
  display.display();
}

// ---------------------------------------------------------------------------
// OLED display: fire status, temperature (with the 5V sense reading
// appended whenever external 5V is detected, or a WiFi tag when the AP is
// up), battery, smoke %. Also
// drives the battery reading update (kept on the same 250ms cadence it
// always ran at) so battery.cpp doesn't need its own separate timer.
// ---------------------------------------------------------------------------
void handleDisplay() {
  unsigned long now = millis();
  if (now - lastDisplayMs < DISPLAY_REFRESH_MS) return;
  lastDisplayMs = now;

  updateBatteryReading();

  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, fireActive ? "** FIRE **" : "Status: Normal");

  display.setFont(ArialMT_Plain_10);
  String tempLine = bmpOk ? ("Temp: " + String(lastTemp, 1) + " C") : String("Temp: N/A");
  if (usbPowerDetected) {
    tempLine += "  5V:" + String(fiveVRailVoltage, 2);
    if (usbVoltageLow) tempLine += "!";
  }
  if (wifiApActive) tempLine += "  WiFi";
  display.drawString(0, 20, tempLine);

  display.drawString(0, 34, "Batt: " + String(batteryPercent) + "% (" + String(batteryVoltage, 2) + "V)");
  display.drawString(0, 48, "Smoke: " + String(dustPercent) + "% (base " + String(dustBaseline) + ")");

  display.display();
}
