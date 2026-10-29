#ifndef POWER_H
#define POWER_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Low power mode (V6). Cuts the node's own consumption WITHOUT touching what
// it exists to do: the radio stays in continuous receive, the mesh code is
// unchanged, and the dust sensor keeps its ~100 Hz pulse train, so smoke is
// still detected and every alert is still received and relayed. What it
// changes (all tunable in config.h): lower CPU clock, loop() idling instead of
// spinning, and the OLED off unless something needs attention.
// ---------------------------------------------------------------------------

enum LowPowerSetting : uint8_t {
  LP_SET_OFF  = 0,   // never
  LP_SET_ON   = 1,   // always (handy for bench current measurements on USB)
  LP_SET_AUTO = 2    // when the node booted without external 5V, i.e. runs from the battery
};

// Call after dustSensorInit() - AUTO needs usbPowerDetected. Loads the saved setting.
void powerInit();

// Call every loop() pass, after handleWifiSwitch(): follows setting / WiFi state
// for the CPU clock, watches the USER button.
void powerService();

// Call last in loop(): sleeps 1 ms when low power mode is active.
void powerIdle();

bool            lowPowerActive();
LowPowerSetting lowPowerSetting();
const char     *lowPowerSettingName();
void            lowPowerSet(LowPowerSetting s);   // applies immediately and saves to flash

// For display.cpp: should the OLED be lit right now?
bool displayWanted();
void powerWakeDisplay();

#endif
