#ifndef BATTERY_H
#define BATTERY_H

// Board has no dedicated VBAT pin - it's read via GPIO1/ADC1_CH0 through a
// fixed onboard divider, gated by GPIO37 (ADC_Ctrl). See config.h for the
// measured VBAT_DIVIDER_RATIO and battery.cpp for the read/scale logic.

void batteryInit();

// Reads GPIO1, updates the shared batteryPercent/batteryVoltage in state.h.
// Currently called from display.cpp on its 250ms refresh cadence; call it
// more often elsewhere if you need fresher battery data independent of the
// display (e.g. a future low-battery alert).
void updateBatteryReading();

#endif
