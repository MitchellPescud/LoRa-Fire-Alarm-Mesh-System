#include <Arduino.h>
#include "dust_sensor.h"
#include "config.h"
#include "state.h"
#include "alarm.h"
#include "display.h"
#include "battery.h"

// ---------------------------------------------------------------------------
// Sharp GP2Y1010AU0F pulse train state - all file-local since no other
// module needs to see the raw ring buffer, only the results (dustPercent,
// dustRawLast, dustBaseline, etc. in state.h).
// ---------------------------------------------------------------------------
static unsigned long lastDustMs  = 0;   // last time handleDustSensor() acted on a reading
static unsigned long lastPulseMs = 0;   // last time the continuous pulse train fired
static unsigned long lastAutoRecalMs = 0;  // last time VBAT drift triggered an auto-recalibration

static int  dustRing[DUST_RING_SIZE];
static int  dustRingIndex  = 0;
static int  dustRingFilled = 0;         // caps at DUST_RING_SIZE
static long dustRingSum    = 0;

// ---------------------------------------------------------------------------
// GP2Y1010AU0F LED-pulse + ADC sample - the low-level primitive. The boost
// converter (5V rail) is held on continuously from dustSensorInit() onward
// (when enabled), so this always samples a fully-settled rail with no
// per-pulse power-up transient.
// ---------------------------------------------------------------------------
static int pulseDustSensor() {
  digitalWrite(PIN_DUST_LED, LOW);            // fire the IR LED
  delayMicroseconds(DUST_LED_SETTLE_US);
  int raw = analogRead(PIN_DUST_ADC);         // sample while LED is on
  delayMicroseconds(DUST_LED_REMAIN_US);
  digitalWrite(PIN_DUST_LED, HIGH);           // LED off
  return raw;
}

static int dustRingAverage() {
  if (dustRingFilled == 0) return 0;
  return (int)(dustRingSum / dustRingFilled);
}

// ---------------------------------------------------------------------------
// Compensates for ESP32-S3 ADC reference drift as VBAT sags. Confirmed via
// scope (on the prior board revision) that the dust sensor's 5V boost rail
// itself stays rock steady regardless of VBAT - the drift lives in the
// chip's own onboard 3.3V/ADC reference (fed from VBAT), not the sensor.
// Since batteryVoltage is read through that same reference, scaling by
// (VBAT at calibration / VBAT now) cancels most of the drift back out.
//
// Real logged sag/recovery events on the prior board revision showed raw
// counts moving ~3x more (in percentage terms) than VBAT itself - not a
// flat 1:1 ratio - so the correction is raised to VBAT_CORRECTION_EXPONENT
// (see config.h) rather than applied linearly. Still an empirical
// approximation - re-verify against real data on this board revision.
//
// Only ever pushes the reading UP relative to the raw value, never down -
// see the max() in the caller. Correction can't tell a real smoke signal
// apart from VBAT-driven drift, so it must never suppress a reading (which
// would only happen when VBAT is above calibrationVbat, e.g. while
// charging) - that direction risks masking a real fire. Boosting suppressed
// readings at low VBAT is safe to apply in full; worst case is a few extra
// false positives, not a missed one.
// ---------------------------------------------------------------------------
static int applyVbatCorrection(int raw) {
  if (calibrationVbat <= 0.0f || batteryVoltage <= 0.0f) return raw;  // no valid reference yet
  float ratio = calibrationVbat / batteryVoltage;
  return (int)(raw * pow(ratio, VBAT_CORRECTION_EXPONENT));
}

// ---------------------------------------------------------------------------
// Reads the 5V-line sense divider (PIN_FIVE_V_SENSE) and decides whether
// the boost converter should be enabled - if external 5V is already
// present (USB or wall), the battery-fed boost must not also be enabled
// onto the same rail. Also flags a low-voltage warning if 5V is present but
// weak (see FIVE_V_LOW_WARNING_THRESHOLD in config.h), since a sagging
// supply might not meet the Sharp sensor's ~4.5V minimum even with the
// boost switched off. Boot-time only - not re-checked while running.
// ---------------------------------------------------------------------------
static void senseFiveVAndSetBoost() {
  pinMode(PIN_FIVE_V_SENSE, INPUT);
  int fiveVRaw = analogRead(PIN_FIVE_V_SENSE);
  float fiveVPinVoltage = (fiveVRaw / (float)ADC_RESOLUTION) * ADC_REF_VOLTAGE * FIVE_V_SENSE_CAL_RATIO;
  // Undo the 10k/10k divider's halving, then subtract the Schottky diode's
  // forward drop - the divider taps the 5V pin before the diode, so *2
  // alone gives the incoming voltage, not what actually reaches the rail
  // after the diode drop.
  fiveVRailVoltage = (fiveVPinVoltage * 2.0f) - SCHOTTKY_DIODE_DROP_V;

  usbPowerDetected = (fiveVPinVoltage >= FIVE_V_PRESENT_THRESHOLD);
  usbVoltageLow = usbPowerDetected && (fiveVPinVoltage < FIVE_V_LOW_WARNING_THRESHOLD);

  Serial.print("[BOOT] 5V sense raw=");
  Serial.print(fiveVRaw);
  Serial.print(" pin=");
  Serial.print(fiveVPinVoltage, 2);
  Serial.print("V (rail~");
  Serial.print(fiveVRailVoltage, 2);
  Serial.print("V) -> ");
  if (!usbPowerDetected) {
    Serial.println("no external 5V, enabling boost converter");
  } else if (usbVoltageLow) {
    Serial.println("external 5V present but LOW, boost OFF - check supply");
  } else {
    Serial.println("external 5V present, boost OFF");
  }

  pinMode(PIN_BOOST_EN, OUTPUT);
  if (usbPowerDetected) {
    digitalWrite(PIN_BOOST_EN, HIGH);  // boost OFF - external 5V already feeds the rail
  } else {
    digitalWrite(PIN_BOOST_EN, LOW);   // boost ON (confirmed near-instant via multimeter)
    delay(BOOST_SETTLE_MS);            // small settle margin before anything reads the dust sensor
  }
}

void dustSensorInit() {
  analogReadResolution(12);   // needed before the 5V-sense read below

  senseFiveVAndSetBoost();

  pinMode(PIN_DUST_LED, OUTPUT);
  digitalWrite(PIN_DUST_LED, HIGH);  // idle HIGH, pulsed LOW to fire the IR LED
}

// ---------------------------------------------------------------------------
// Keeps the GP2Y1010 pulsing continuously at its datasheet-specified ~10ms
// cycle, independent of how often handleDustSensor() actually acts on a
// reading. Each pulse is folded into a rolling ring-buffer average, which
// is what handleDustSensor() (and calibrateBaseline()) read from - live
// operation and calibration are sampled by the exact same mechanism, at
// the exact same spec-correct cadence.
// ---------------------------------------------------------------------------
void serviceDustPulseTrain() {
  unsigned long now = millis();
  if (now - lastPulseMs < DUST_PULSE_CYCLE_MS) return;
  lastPulseMs = now;

  int raw = pulseDustSensor();

  dustRingSum -= dustRing[dustRingIndex];
  dustRing[dustRingIndex] = raw;
  dustRingSum += raw;
  dustRingIndex = (dustRingIndex + 1) % DUST_RING_SIZE;
  if (dustRingFilled < DUST_RING_SIZE) dustRingFilled++;
}

// ---------------------------------------------------------------------------
// Sustained above/below-baseline classification -> alarm trigger/clear.
// ---------------------------------------------------------------------------
static void evaluateSmokeState(int delta) {
  if (!smokePresent) {
    if (delta >= SMOKE_TRIGGER_DELTA) {
      highReadingCount++;
      lowReadingCount = 0;
      if (highReadingCount >= REQUIRED_HIGH_READINGS) {
        smokePresent = true;
        highReadingCount = 0;
        triggerAlarm();
      }
    } else {
      highReadingCount = 0;
    }
  } else {
    if (delta <= SMOKE_CLEAR_MARGIN) {
      lowReadingCount++;
      highReadingCount = 0;
      if (lowReadingCount >= REQUIRED_LOW_READINGS) {
        smokePresent = false;
        lowReadingCount = 0;
        if (!LATCH_ALARM) {
          clearAlarm("[INFO] Smoke cleared - alarm auto-reset");
        }
      }
    } else {
      lowReadingCount = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Acts on the dust sensor's rolling average at DUST_SAMPLE_MS normally, or
// DUST_FAST_SAMPLE_MS once a reading looks elevated or the alarm is active.
// The ring-buffer average is always current (serviceDustPulseTrain() keeps
// it updated independently of this function's own cadence), so we can peek
// at it every loop() pass to decide the polling speed even before it's
// time to act on a reading - that's what lets fast-polling kick in the
// moment delta crosses EARLY_WARNING_DELTA, rather than only after
// highReadingCount has already started climbing toward the full alarm
// threshold.
// ---------------------------------------------------------------------------
void handleDustSensor() {
  unsigned long now = millis();
  if (dustRingFilled < DUST_RING_SIZE) return;  // wait for a full window of continuous-pulse data

  int physicalRaw = dustRingAverage();
  // Never let the correction reduce the reading below its raw value - see
  // the comment on applyVbatCorrection() above.
  int currentRaw = max(physicalRaw, applyVbatCorrection(physicalRaw));
  int currentDelta = baselineReady ? (currentRaw - dustBaseline) : 0;

  bool fastPoll = (currentDelta >= EARLY_WARNING_DELTA) || (highReadingCount > 0) || fireActive;
  unsigned long interval = fastPoll ? DUST_FAST_SAMPLE_MS : DUST_SAMPLE_MS;
  if (now - lastDustMs < interval) return;
  lastDustMs = now;

  // For deciding whether it's SAFE to auto-recalibrate, use the full
  // bidirectional correction (not currentDelta above) - currentDelta is
  // deliberately never scaled down when VBAT is high, so it stays looking
  // "elevated" for as long as the board is charging even with zero real
  // smoke, which would otherwise lock auto-recal out entirely while on
  // charge. A genuine smoke delta is large enough (up toward
  // SMOKE_TRIGGER_DELTA) that it still clears EARLY_WARNING_DELTA even
  // after this down-scaling, so this doesn't reopen the masking risk -
  // it just stops VBAT-only elevation from blocking recalibration.
  int fullyCorrectedDelta = baselineReady
    ? (applyVbatCorrection(physicalRaw) - dustBaseline)
    : 0;

  // If VBAT has drifted significantly since the last calibration and the
  // reading currently looks clean, get a fresh accurate baseline instead of
  // leaning on applyVbatCorrection() to keep stretching further from its
  // fitted range. Hard-guarded against ever running near real smoke - see
  // config.h.
  if (baselineReady && !fireActive
      && fabs(batteryVoltage - calibrationVbat) >= VBAT_RECAL_THRESHOLD
      && abs(fullyCorrectedDelta) < EARLY_WARNING_DELTA
      && (now - lastAutoRecalMs) >= VBAT_RECAL_COOLDOWN_MS) {
    Serial.println("[INFO] VBAT drifted significantly with a clean reading - auto-recalibrating baseline");
    lastAutoRecalMs = now;
    calibrateBaseline();
    return;
  }

  int raw = currentRaw;
  dustRawLast = raw;

  int clamped = constrain(raw, DUST_ADC_MIN, DUST_ADC_MAX);
  dustPercent = map(clamped, DUST_ADC_MIN, DUST_ADC_MAX, 0, 100);

  Serial.print("[DUST] raw=");
  Serial.print(raw);
  Serial.print(" percent=");
  Serial.print(dustPercent);
  Serial.print("%");

  if (baselineReady) {
    int delta = raw - dustBaseline;
    Serial.print(" baseline=");
    Serial.print(dustBaseline);
    Serial.print(" delta=");
    Serial.print(delta);
    evaluateSmokeState(delta);
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// Baseline calibration: runs the continuous ~10ms pulse train for
// BASELINE_WARMUP_MS to let it get established, then takes the median of
// BASELINE_SAMPLE_COUNT more pulses (at the same cadence) as the baseline -
// median rather than mean so a lone outlier/glitch sample can't skew it.
// Blocking by design - only ever called from setup() or between readings
// while the alarm is inactive, never mid-alarm.
// ---------------------------------------------------------------------------
void calibrateBaseline() {
  Serial.println("[BOOT] Calibrating dust baseline - keep area smoke-free...");
  Serial.println("[CAL] Running continuous ~10ms pulse train (per datasheet) to settle first...");

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "Calibrating");
  display.drawString(0, 12, "baseline...");
  display.drawString(0, 24, "Keep area clear");
  if (!usbPowerDetected) {
    display.drawString(0, 36, "5V: Battery (boost)");
  } else if (usbVoltageLow) {
    display.drawString(0, 36, "5V: USB (LOW!)");
  } else {
    display.drawString(0, 36, "5V: USB");
  }
  display.display();

  unsigned long warmupStart = millis();
  unsigned long nextPulseMs = warmupStart;
  while (millis() - warmupStart < BASELINE_WARMUP_MS) {
    if (millis() - nextPulseMs >= DUST_PULSE_CYCLE_MS) {
      nextPulseMs = millis();
      pulseDustSensor();   // keep the sensor in its spec'd pulse train; discarded during warm-up
    }
  }

  int samples[BASELINE_SAMPLE_COUNT];
  for (unsigned int i = 0; i < BASELINE_SAMPLE_COUNT; i++) {
    unsigned long pulseStart = millis();
    int raw = pulseDustSensor();
    samples[i] = raw;

    Serial.print("[CAL] sample ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.print(BASELINE_SAMPLE_COUNT);
    Serial.print(" raw=");
    Serial.println(raw);

    while (millis() - pulseStart < DUST_PULSE_CYCLE_MS) { /* hold to the 10ms pulse cycle */ }
  }

  // Median instead of a plain mean - robust against a handful of outlier
  // samples (occasional spurious raw=0 glitches, or a lone low/high pulse)
  // that would otherwise skew a straight average. BASELINE_SAMPLE_COUNT is
  // small, so a plain insertion sort is plenty fast here.
  for (unsigned int i = 1; i < BASELINE_SAMPLE_COUNT; i++) {
    int key = samples[i];
    int j = (int)i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }
  unsigned int mid = BASELINE_SAMPLE_COUNT / 2;
  dustBaseline = (BASELINE_SAMPLE_COUNT % 2 == 0)
    ? (samples[mid - 1] + samples[mid]) / 2
    : samples[mid];
  baselineReady = true;

  // Record VBAT at this exact moment as the reference point for
  // applyVbatCorrection() - see the comment above that function.
  updateBatteryReading();
  calibrationVbat = batteryVoltage;
  highReadingCount = lowReadingCount = 0;

  // Seed the live ring buffer with the baseline so handleDustSensor() has a
  // full window immediately instead of waiting ~80ms for it to fill.
  for (int i = 0; i < DUST_RING_SIZE; i++) dustRing[i] = dustBaseline;
  dustRingSum = (long)dustBaseline * DUST_RING_SIZE;
  dustRingFilled = DUST_RING_SIZE;
  dustRingIndex = 0;
  lastPulseMs = millis();

  Serial.print("[BOOT] Dust baseline = ");
  Serial.println(dustBaseline);
}
