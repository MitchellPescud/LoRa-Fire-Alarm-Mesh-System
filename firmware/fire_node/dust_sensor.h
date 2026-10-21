#ifndef DUST_SENSOR_H
#define DUST_SENSOR_H

// Reads the 5V-line sense divider to decide whether the boost converter
// should be enabled at all, then sets up the boost + LED pins. Boost is
// left on continuously once enabled - see config.h / dust_sensor.cpp
// comments for why (datasheet requires a continuous ~10ms pulse train, and
// gating the boost per-reading was tried and abandoned on the prior board
// revision - it left a turn-on transient that skewed readings).
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
// Call from setup(), or on-demand (the "baseline" serial command) - only
// while the alarm is inactive.
void calibrateBaseline();

#endif
