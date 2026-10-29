// Host test for the V6 battery filter (battery_filter.h) using the REAL constants
// from config.h. Build/run from tests/:
//   g++ -std=c++11 -Wall -I.. test_battery.cpp -o test_battery && ./test_battery
//
// The noise model below is an ASSUMPTION chosen to look like what the bench unit
// shows ("jumps around ~5%"): ~1.5 counts of ADC noise at the pin, which the 5.36x
// divider ratio turns into about 0.0065 V sigma (~0.7% of the
// 3.3-4.2 V span), plus a small share of readings landing in a TX/WiFi current sag. Re-tune NOISE_COUNTS
// / SAG_* against logged data from a real board; the PASS/FAIL checks on the
// filter's behaviour (spike rejection, settling, no start-up ramp) do not depend
// on the noise model.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <random>
#include "../config.h"
#include "../battery_filter.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- simulated hardware ---------------------------------------------------
static const double NOISE_COUNTS   = 1.5;     // ADC noise sigma at the pin (counts)
static const double SAG_PROB       = 0.03;    // share of readings that land inside a TX / WiFi current burst
static const double SAG_VOLTS      = 0.04;    // battery sag during that burst
static std::mt19937 rng(12345);
static double gauss() {
  double u1 = (rng() + 1.0) / 4294967297.0, u2 = (rng() + 1.0) / 4294967297.0;
  return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}
static int adcCountsFor(double pinVolts) {
  double c = pinVolts / ADC_REF_VOLTAGE * ADC_RESOLUTION + gauss() * NOISE_COUNTS;
  if (c < 0) c = 0;
  if (c > ADC_RESOLUTION) c = ADC_RESOLUTION;
  return (int)(c + 0.5);
}
static double toVolts(double counts) { return counts / ADC_RESOLUTION * ADC_REF_VOLTAGE * VBAT_DIVIDER_RATIO; }
static int toPercent(double v) {
  double p = (v - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return (int)(p + 0.5);
}

// What V5 did: one analogRead() per reading.
static double v5Reading(double trueV, bool sag) {
  double v = trueV - (sag ? SAG_VOLTS : 0.0);
  return toVolts(adcCountsFor(v / VBAT_DIVIDER_RATIO));
}
// What V6 does for one reading: burst of samples, trimmed mean (sag affects the whole 2 ms burst).
static double v6Reading(double trueV, bool sag) {
  int s[BATT_BURST_SAMPLES];
  double v = trueV - (sag ? SAG_VOLTS : 0.0);
  for (int i = 0; i < BATT_BURST_SAMPLES; i++) s[i] = adcCountsFor(v / VBAT_DIVIDER_RATIO);
  return toVolts(battTrimmedMean(s, BATT_BURST_SAMPLES, BATT_BURST_TRIM));
}

struct Stats { int minP, maxP, changes; double maxStep; };

// Runs `seconds` of a steady battery and reports the displayed-percentage spread.
static Stats runSteady(bool v6, double trueV, int seconds, bool withSag) {
  Stats st = {1000, -1, 0, 0};
  BatteryFilter f;
  int shown = -1, prev = -1;
  double period = v6 ? BATT_SAMPLE_MS / 1000.0 : 0.25;     // V5 read on the 250 ms display cadence
  // warm-up so we measure steady state, not the boot seed
  int n = (int)(seconds / period);
  double seed = 0; for (int i = 0; i < BATT_SEED_BURSTS; i++) seed += v6 ? v6Reading(trueV, false) : v5Reading(trueV, false);
  seed /= BATT_SEED_BURSTS;
  if (v6) { f.reset(seed, BATT_MEDIAN_WINDOW); shown = toPercent(seed); }
  for (int i = 0; i < n; i++) {
    bool sag = withSag && ((rng() % 10000) < SAG_PROB * 10000);
    int p;
    if (v6) {
      double r = v6Reading(trueV, sag);
      double sm = f.push(r, BATT_EMA_ALPHA);
      shown = battApplyDeadband(shown, toPercent(sm), BATT_PCT_DEADBAND);
      p = shown;
    } else {
      p = toPercent(v5Reading(trueV, sag));
    }
    if (p < st.minP) st.minP = p;
    if (p > st.maxP) st.maxP = p;
    if (prev >= 0 && p != prev) { st.changes++; if (abs(p - prev) > st.maxStep) st.maxStep = abs(p - prev); }
    prev = p;
  }
  return st;
}

int main() {
  printf("== battery filter host test (window=%d, alpha=%.2f, burst=%d trim=%d, every %lu ms) ==\n",
         BATT_MEDIAN_WINDOW, BATT_EMA_ALPHA, BATT_BURST_SAMPLES, BATT_BURST_TRIM, BATT_SAMPLE_MS);

  // 1. trimmed mean ignores outliers
  { int v[8] = {2000, 2001, 1999, 2000, 0, 4095, 2002, 1998};
    float m = battTrimmedMean(v, 8, 2);
    printf("[1] trimmed mean with two wild samples = %.2f (true 2000)\n", m);
    CHECK(fabs(m - 2000.5) < 1.5, "trimmed mean %.2f", m);
    int w[4] = {10, 20, 30, 40};
    CHECK(fabs(battTrimmedMean(w, 4, 5) - 25.0) < 1e-6, "trim too large must fall back to plain mean");
    CHECK(battTrimmedMean(w, 0, 1) == 0.0f, "n=0"); }

  // 2. start-up: no ramp from zero
  { BatteryFilter f; f.reset(3.90f, BATT_MEDIAN_WINDOW);
    float o = f.push(3.91f, BATT_EMA_ALPHA);
    printf("[2] first push after seeding 3.90 -> %.3f\n", o);
    CHECK(fabs(o - 3.90) < 0.01, "seeded filter jumped to %.3f", o);
    BatteryFilter g; float first = g.push(3.77f, BATT_EMA_ALPHA);     // unseeded: first value seeds it
    CHECK(fabs(first - 3.77) < 1e-6, "unseeded first push should return the value itself"); }

  // 3. short disturbances are rejected (median), longer than half the window is real
  { BatteryFilter f; f.reset(3.80f, BATT_MEDIAN_WINDOW);
    double worst = 0;
    for (int i = 0; i < 4; i++) { double o = f.push(3.50f, BATT_EMA_ALPHA); worst = fmax(worst, fabs(o - 3.80)); }   // 4 readings = 2 s of -0.3 V
    for (int i = 0; i < 20; i++) { double o = f.push(3.80f, BATT_EMA_ALPHA); worst = fmax(worst, fabs(o - 3.80)); }
    printf("[3] 4 consecutive -0.30 V readings (2 s): worst output error = %.4f V\n", worst);
    CHECK(worst < 0.005, "short dip leaked through: %.4f V", worst); }

  // 4. a real step is followed
  { BatteryFilter f; f.reset(3.80f, BATT_MEDIAN_WINDOW);
    int settled = -1; double o = 0;
    for (int i = 1; i <= 60; i++) { o = f.push(3.70f, BATT_EMA_ALPHA); if (settled < 0 && fabs(o - 3.70) < 0.01) settled = i; }
    double secs = settled * BATT_SAMPLE_MS / 1000.0;
    printf("[4] true 3.80 -> 3.70 V step: within 10 mV after %d readings (%.1f s)\n", settled, secs);
    CHECK(settled > 0 && secs <= 15.0, "step not followed in time (settled=%d)", settled);
    CHECK(fabs(o - 3.70) < 0.002, "final value %.4f", o); }

  // 5. deadband
  { CHECK(battApplyDeadband(62, 63, 2) == 62, "63 vs 62 within deadband");
    CHECK(battApplyDeadband(62, 64, 2) == 64, "64 vs 62 should move");
    CHECK(battApplyDeadband(62, 61, 2) == 62, "61 vs 62 within deadband");
    CHECK(battApplyDeadband(3, 0, 2) == 0, "0%% always accepted");
    CHECK(battApplyDeadband(98, 100, 2) == 100, "100%% always accepted");
    printf("[5] deadband rules ok\n"); }

  // 6. the actual symptom: spread of the displayed percentage on a steady battery
  { const double V = 3.80;   // ~55%
    Stats a = runSteady(false, V, 300, true);
    Stats b = runSteady(true,  V, 300, true);
    printf("[6] steady 3.80 V, 5 min, with TX/WiFi sag:\n");
    printf("      V5 (single read, 250 ms): shown %d%%..%d%% (spread %d), %d changes, biggest jump %.0f\n", a.minP, a.maxP, a.maxP - a.minP, a.changes, a.maxStep);
    printf("      V6 (filtered, 500 ms)   : shown %d%%..%d%% (spread %d), %d changes, biggest jump %.0f\n", b.minP, b.maxP, b.maxP - b.minP, b.changes, b.maxStep);
    CHECK(a.maxP - a.minP >= 5, "model should reproduce the V5 symptom (spread >= 5), got %d", a.maxP - a.minP);
    CHECK(b.maxP - b.minP <= 3, "V6 spread %d too large", b.maxP - b.minP);
    CHECK(b.changes * 10 <= a.changes, "V6 should change the readout at least 10x less often (%d vs %d)", b.changes, a.changes); }

  // 7. slow discharge is tracked: 4.00 V -> 3.90 V over 10 minutes
  { BatteryFilter f; double v0 = 4.00; f.reset(v0, BATT_MEDIAN_WINDOW);
    double maxLag = 0, o = v0;
    int n = (int)(600 / (BATT_SAMPLE_MS / 1000.0));
    for (int i = 0; i < n; i++) {
      double tv = 4.00 - 0.10 * i / n;
      bool sag = (rng() % 10000) < SAG_PROB * 10000;
      o = f.push(v6Reading(tv, sag), BATT_EMA_ALPHA);
      if (i > 20) maxLag = fmax(maxLag, fabs(o - tv));
    }
    printf("[7] 10-minute drain 4.00->3.90 V: worst |filtered - true| = %.4f V\n", maxLag);
    CHECK(maxLag < 0.02, "filter lags the true voltage by %.4f V", maxLag); }

  if (failures) { printf("\n%d CHECK(S) FAILED\n", failures); return 1; }
  printf("\nALL BATTERY FILTER CHECKS PASSED\n");
  return 0;
}
