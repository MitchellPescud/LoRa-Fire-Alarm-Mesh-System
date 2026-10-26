#include <Arduino.h>
#include "battery.h"
#include "config.h"
#include "state.h"

void batteryInit() {
  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, HIGH);  // enable VBAT divider so GPIO1 reads battery voltage
}

// ---------------------------------------------------------------------------
// VBAT = V_ADC * VBAT_DIVIDER_RATIO (measured, see config.h). Percentage is
// a simple linear approximation between empty/full pack voltage, not the
// real Li-ion discharge curve.
// ---------------------------------------------------------------------------
void updateBatteryReading() {
  int raw = analogRead(PIN_VBAT);
  float voltage = (raw / (float)ADC_RESOLUTION) * ADC_REF_VOLTAGE * VBAT_DIVIDER_RATIO;

  float pct = (voltage - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  batteryVoltage = voltage;
  batteryPercent = (int)(pct + 0.5f);
}
