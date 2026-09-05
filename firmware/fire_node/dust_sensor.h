#ifndef DUST_SENSOR_H
#define DUST_SENSOR_H

// Boost converter + LED pin setup. Boost is left on continuously - see
// config.h / dust_sensor.cpp comments for why (datasheet requires a
// continuous ~10ms pulse train, and gating the boost per-reading was tried
// and abandoned - it left a turn-on transient that skewed readings).
void dustSensorInit();

// Keeps the GP2Y1010 pulsing continuously at its datasheet-specified
// ~10ms cycle. Self-gated - safe to call every loop() pass.
void serviceDustPulseTrain();

// Acts on the rolling pulse-train average at the normal or fast decision
// cadence (see config.h), evaluates it against the baseline, and drives
// the alarm via triggerAlarm()/clearAlarm() (alarm.h) when appropriate.
void handleDustSensor();

// Blocking: runs the continuous pulse train for BASELINE_WARMUP_MS then
// takes the median of BASELINE_SAMPLE_COUNT more pulses as the baseline.
// Call from setup(), or on-demand (the "baseline" serial command / the
// dashboard's recalibrate button) - only while the alarm is inactive.
void calibrateBaseline();

// Re-checks the USB 5V sense pin every loop() pass and toggles the boost
// converter if the state changes while running - not just what was true at
// boot in dustSensorInit(). Edge-triggered (only acts on a change), cheap
// enough to call unconditionally every pass.
void handleUsbSense();

#endif
