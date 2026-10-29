#include <Arduino.h>
#include "battery.h"
#include "battery_filter.h"
#include "config.h"
#include "state.h"

static BatteryFilter filt;
static unsigned long lastSampleMs = 0;

// ---------------------------------------------------------------------------
// VBAT = V_ADC * VBAT_DIVIDER_RATIO (measured, see config.h). Percentage is
// a simple linear approximation between empty/full pack voltage, not the
// real Li-ion discharge curve.
// ---------------------------------------------------------------------------
static float countsToVolts(float counts) {
  return (counts / (float)ADC_RESOLUTION) * ADC_REF_VOLTAGE * VBAT_DIVIDER_RATIO;
}

// One "reading": BATT_BURST_SAMPLES ADC samples a hair apart, extremes
// trimmed, averaged. About 2 ms in total.
static float readBurstVolts() {
  int s[BATT_BURST_SAMPLES];
  for (int i = 0; i < BATT_BURST_SAMPLES; i++) {
    s[i] = analogRead(PIN_VBAT);
    delayMicroseconds(BATT_BURST_GAP_US);
  }
  return countsToVolts(battTrimmedMean(s, BATT_BURST_SAMPLES, BATT_BURST_TRIM));
}

static int voltsToPercent(float v) {
  float pct = (v - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (int)(pct + 0.5f);
}

static void takeReading() {
  float instant = readBurstVolts();
  float smooth  = filt.push(instant, BATT_EMA_ALPHA);

  batteryVoltageInstant = instant;
  batteryVoltage = smooth;
  batteryPercent = battApplyDeadband(batteryPercent, voltsToPercent(smooth), BATT_PCT_DEADBAND);
}

void batteryInit() {
  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, HIGH);  // enable VBAT divider so GPIO1 reads battery voltage
  delay(BATT_DIVIDER_SETTLE_MS);     // divider + ADC input need a moment before the first read

  // Seed the filter with the average of a few bursts so the very first
  // value (and the percentage on the OLED) is already close to the truth.
  float sum = 0;
  for (int i = 0; i < BATT_SEED_BURSTS; i++) {
    sum += readBurstVolts();
    delay(10);
  }
  float v = sum / BATT_SEED_BURSTS;
  filt.reset(v, BATT_MEDIAN_WINDOW);
  batteryVoltageInstant = v;
  batteryVoltage = v;
  batteryPercent = voltsToPercent(v);
  lastSampleMs = millis();
}

void batteryService() {
  unsigned long now = millis();
  if (now - lastSampleMs < BATT_SAMPLE_MS) return;
  lastSampleMs = now;
  takeReading();
}

void updateBatteryReading() {
  lastSampleMs = millis();
  takeReading();
}
