#ifndef BATTERY_FILTER_H
#define BATTERY_FILTER_H

// ---------------------------------------------------------------------------
// Pure filtering maths for the battery reading - no Arduino or config.h
// dependency, so the exact same code is unit-tested on a PC (tests/test_battery.cpp).
//
// Three stages, each aimed at a different source of the +/-5% jumping:
//   1. battTrimmedMean(): one reading = many back-to-back ADC samples with
//      the extremes thrown away. Removes the ESP32-S3 ADC's sample-to-sample
//      noise (a few LSB at the pin is several LSB x 5.36 at the battery).
//   2. BatteryFilter median window: the median of the last N readings. Real
//      short dips - a LoRa TX burst or the WiFi radio pulling a few hundred mA
//      through the cell's internal resistance - only hit one or two readings
//      and are rejected outright instead of being averaged in.
//   3. BatteryFilter EMA: smooths what is left. The cell voltage only moves
//      over minutes/hours, so a few seconds of lag costs nothing.
// ---------------------------------------------------------------------------

// Sorts v[0..n) in place, drops `trim` values from each end, returns the mean
// of the rest. Falls back to the plain mean if trim would remove everything.
inline float battTrimmedMean(int *v, int n, int trim) {
  if (n <= 0) return 0.0f;
  for (int i = 1; i < n; i++) {                     // insertion sort - n is small
    int key = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  if (trim < 0 || 2 * trim >= n) trim = 0;
  long sum = 0;
  for (int i = trim; i < n - trim; i++) sum += v[i];
  return (float)sum / (float)(n - 2 * trim);
}

struct BatteryFilter {
  static const int MAX_WINDOW = 31;

  float win[MAX_WINDOW];
  int   windowSize;      // active window length (1..MAX_WINDOW)
  int   count;           // readings currently in the window (<= windowSize)
  int   head;            // next slot to overwrite
  float ema;
  bool  seeded;

  BatteryFilter() : windowSize(9), count(0), head(0), ema(0.0f), seeded(false) {}

  // Start over with `first` as the whole history (used at boot so the display
  // does not crawl up from 0%).
  void reset(float first, int window) {
    if (window < 1) window = 1;
    if (window > MAX_WINDOW) window = MAX_WINDOW;
    windowSize = window;
    for (int i = 0; i < windowSize; i++) win[i] = first;
    count = windowSize;
    head = 0;
    ema = first;
    seeded = true;
  }

  float median() const {
    float tmp[MAX_WINDOW];
    int n = count;
    for (int i = 0; i < n; i++) tmp[i] = win[i];
    for (int i = 1; i < n; i++) {
      float key = tmp[i];
      int j = i - 1;
      while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; j--; }
      tmp[j + 1] = key;
    }
    if (n == 0) return 0.0f;
    return (n % 2) ? tmp[n / 2] : 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
  }

  // Feed one reading, get the filtered value back. `alpha` is the EMA weight
  // of the newest median (0..1; smaller = smoother).
  float push(float v, float alpha) {
    if (!seeded) { reset(v, windowSize); return v; }
    win[head] = v;
    head = (head + 1) % windowSize;
    ema += alpha * (median() - ema);
    return ema;
  }
};

// Percentage shown/sent to the mesh. Moves only when the new value differs by
// at least `deadband` points (so a reading sitting on a rounding boundary does
// not flicker 62/63/62), except that 0% and 100% are always accepted.
inline int battApplyDeadband(int shown, int fresh, int deadband) {
  if (fresh <= 0 || fresh >= 100) return fresh;
  int d = fresh - shown;
  if (d < 0) d = -d;
  return (d >= deadband) ? fresh : shown;
}

#endif
