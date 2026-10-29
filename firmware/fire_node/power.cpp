#include <Arduino.h>
#include <Preferences.h>
#include "power.h"
#include "config.h"
#include "state.h"

static LowPowerSetting setting = (LowPowerSetting)LOWPOWER_DEFAULT_SETTING;
static bool          lpActive    = false;
static uint32_t      bootMhz     = 240;     // clock the sketch started at; restored when low power ends
static unsigned long wakeUntilMs = 0;
static bool          lastWifiUp  = false;
static bool          btnDown     = false;
static unsigned long btnChangeMs = 0;

static bool computeActive() {
  if (setting == LP_SET_ON)  return true;
  if (setting == LP_SET_OFF) return false;
  // AUTO. usbPowerDetected is the boot-time 5V check (see dust_sensor.cpp), so
  // this follows how the node was powered when it started.
  return !usbPowerDetected;
}

static void applyClock() {
  uint32_t want = bootMhz;
  if (lpActive) {
    want = wifiApActive ? LP_CPU_MHZ_WIFI : LP_CPU_MHZ;
    if (want > bootMhz) want = bootMhz;
  }
  uint32_t have = getCpuFrequencyMhz();
  if (want != have) {
    setCpuFrequencyMhz(want);
    Serial.print("[POWER] CPU ");
    Serial.print(have);
    Serial.print(" -> ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");
  }
}

const char *lowPowerSettingName() {
  switch (setting) {
    case LP_SET_ON:  return "on";
    case LP_SET_OFF: return "off";
    default:         return "auto";
  }
}

static void announce() {
  Serial.print("[POWER] low power setting=");
  Serial.print(lowPowerSettingName());
  Serial.print(" -> ");
  Serial.print(lpActive ? "ACTIVE" : "inactive");
  if (setting == LP_SET_AUTO) Serial.print(usbPowerDetected ? " (booted on external 5V)" : " (running from battery)");
  Serial.print(", CPU ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");
}

void powerInit() {
  bootMhz = getCpuFrequencyMhz();

  Preferences p;
  if (p.begin("power", false)) {
    uint8_t v = p.getUChar("lp", (uint8_t)LOWPOWER_DEFAULT_SETTING);
    if (v <= LP_SET_AUTO) setting = (LowPowerSetting)v;
    p.end();
  }

  if (PIN_USER_BTN >= 0) pinMode(PIN_USER_BTN, INPUT_PULLUP);

  lpActive = computeActive();
  wakeUntilMs = millis() + LP_DISPLAY_WAKE_MS;   // show the boot state for a while
  applyClock();
  announce();
}

bool            lowPowerActive()  { return lpActive; }
LowPowerSetting lowPowerSetting() { return setting; }

void powerWakeDisplay() { wakeUntilMs = millis() + LP_DISPLAY_WAKE_MS; }

void lowPowerSet(LowPowerSetting s) {
  setting = s;
  Preferences p;
  if (p.begin("power", false)) {
    p.putUChar("lp", (uint8_t)s);
    p.end();
  }
  lpActive = computeActive();
  applyClock();
  powerWakeDisplay();
  announce();
}

bool displayWanted() {
  if (!lpActive) return true;
  if (fireActive) return true;
  return (long)(wakeUntilMs - millis()) > 0;
}

void powerService() {
  unsigned long now = millis();

  // WiFi AP started/stopped: re-pick the clock (WiFi needs >= 80 MHz) and show
  // the OLED so the person flipping the switch sees what happened.
  if (wifiApActive != lastWifiUp) {
    lastWifiUp = wifiApActive;
    powerWakeDisplay();
    applyClock();
  }

  // USER button wakes the OLED.
  if (PIN_USER_BTN >= 0) {
    bool down = (digitalRead(PIN_USER_BTN) == LOW);
    if (down != btnDown && (now - btnChangeMs) >= LP_BUTTON_DEBOUNCE_MS) {
      btnDown = down;
      btnChangeMs = now;
      if (down) powerWakeDisplay();
    }
  }
}

void powerIdle() {
  if (lpActive) delay(1);   // lets the idle task halt the core between passes
}
