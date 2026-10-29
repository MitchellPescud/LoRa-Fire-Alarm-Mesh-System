#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "state.h"
#include "battery.h"
#include "mesh.h"
#include "power.h"

static unsigned long lastDisplayMs = 0;
static bool oledOn = true;

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
// up), battery, smoke %.
//
// V6: the battery reading no longer rides on this refresh (batteryService()
// has its own timer), so it keeps running while the OLED is off in low power
// mode. In low power mode the panel is switched off unless an alarm is
// active or something woke it (USER button, WiFi switch) - see power.cpp.
// ---------------------------------------------------------------------------
void handleDisplay() {
  unsigned long now = millis();

  if (!displayWanted()) {
    if (oledOn) {
      display.displayOff();
      oledOn = false;
    }
    return;
  }
  if (!oledOn) {
    display.displayOn();
    oledOn = true;
    lastDisplayMs = 0;            // redraw straight away
  }

  if (now - lastDisplayMs < DISPLAY_REFRESH_MS) return;
  lastDisplayMs = now;

  display.clear();
  display.setFont(ArialMT_Plain_16);
  if (!fireActive) {
    display.drawString(0, 0, "Status: Normal");
  } else if (meshLocalEpisodeActive()) {
    display.drawString(0, 0, "** FIRE **");
  } else {
    display.drawString(0, 0, "FIRE node " + String(meshFirstAlarmOrigin()));   // alarm raised by another node
  }

  display.setFont(ArialMT_Plain_10);
  String tempLine = bmpOk ? ("Temp: " + String(lastTemp, 1) + " C") : String("Temp: N/A");
  if (usbPowerDetected) {
    tempLine += "  5V:" + String(fiveVRailVoltage, 2);
    if (usbVoltageLow) tempLine += "!";
  }
  if (wifiApActive) tempLine += "  WiFi";
  if (lowPowerActive()) tempLine += "  LP";
  display.drawString(0, 20, tempLine);

  display.drawString(0, 34, "Batt: " + String(batteryPercent) + "% " + String(batteryVoltage, 2) + "V N:" + String(meshPeerCount()));
  display.drawString(0, 48, "Smoke: " + String(dustPercent) + "% (base " + String(dustBaseline) + ")");

  display.display();
}
