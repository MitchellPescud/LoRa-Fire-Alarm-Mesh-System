#ifndef BATTERY_H
#define BATTERY_H

// Board has no dedicated VBAT pin - it's read via GPIO1/ADC1_CH0 through a
// fixed onboard divider, gated by GPIO37 (ADC_Ctrl). See config.h for the
// measured VBAT_DIVIDER_RATIO and battery.cpp for the read/scale logic.
//
// V6: the reading is filtered (burst trimmed-mean -> median window -> EMA, see
// battery_filter.h) and batteryVoltage / batteryPercent in state.h hold the
// FILTERED value. batteryVoltageInstant is the unfiltered burst, kept only for
// diagnostics (serial "battery" command).

// Pin setup + first filtered value, so nothing ever sees 0%.
void batteryInit();

// Call every loop() pass; takes a new reading every BATT_SAMPLE_MS. This used
// to be piggy-backed on the display refresh, which would silently freeze the
// battery value (and the dust sensor's VBAT correction that depends on it)
// whenever the OLED is off in low power mode - so it now runs on its own.
void batteryService();

// Take a reading right now and fold it into the filter (used when the
// baseline is calibrated so calibrationVbat is current).
void updateBatteryReading();

#endif
