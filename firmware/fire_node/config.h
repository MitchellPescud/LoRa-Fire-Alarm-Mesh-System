#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
// Pin assignments - final designed board revision (Heltec WiFi LoRa 32 V4.3
// / ESP32-S3). WiFi and status-LED ("light") functionality are not wired on
// this board - only Heltec base, piezo buzzer, boost converter, smoke
// sensor and temp sensor are populated.
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA       17   // shared OLED + BMP280 bus
#define PIN_I2C_SCL       18
#define PIN_BUZZER        15   // active HIGH (moved from GPIO16 on the prior board revision)
// GPIO16 is the nominal WiFi-switch position on this board but is not
// wired - WiFi isn't implemented in this build.
#define PIN_BOOST_EN      48   // LOW = boost converter (5V dust sensor rail) ON
#define PIN_DUST_LED      47   // GP2Y1010 LED drive (active LOW pulse)
#define PIN_DUST_ADC      6    // GP2Y1010 Vo (moved from GPIO3 on the prior board revision)
#define PIN_FIVE_V_SENSE  3    // 10k/10k divider off the board's 5V line - decides boost enable
#define PIN_VBAT          1    // ADC1_CH0, battery voltage via onboard divider
#define PIN_ADC_CTRL      37   // HIGH = enable VBAT divider
#define PIN_VEXT_CTRL     36   // LOW = Vext (3.3V rail: OLED + BMP280) powered on

// ---------------------------------------------------------------------------
// Node identity
// ---------------------------------------------------------------------------
#define NODE_ID          "node1"

// ---------------------------------------------------------------------------
// General timing
// ---------------------------------------------------------------------------
const unsigned long BUZZ_INTERVAL_MS    = 1000;  // one chirp per second
const unsigned long BUZZ_ON_MS          = 150;   // chirp length
const unsigned long DUST_SAMPLE_MS      = 1000;  // normal decision cadence
const unsigned long DUST_FAST_SAMPLE_MS = 250;   // faster decision cadence once elevated/alarm active
const unsigned long DISPLAY_REFRESH_MS  = 250;
const unsigned long TEMP_POLL_MS        = 500;   // poll BMP280 once every 500ms

// ---------------------------------------------------------------------------
// Sharp GP2Y1010AU0F pulse timing (per datasheet Fig. 2 / timing table):
//   Pulse Cycle  T  = 10ms +/-1ms    - LED pulsed continuously at ~100Hz
//   Pulse Width  Pw = 0.32ms +/-0.02ms (320us) - matches SETTLE+REMAIN below
// The sensor's rated "no dust" output is characterized under this continuous
// pulse train, not isolated single-shot pulses - see dust_sensor.cpp.
// ---------------------------------------------------------------------------
const unsigned long DUST_PULSE_CYCLE_MS = 10;
const unsigned int  DUST_LED_SETTLE_US  = 280;
const unsigned int  DUST_LED_REMAIN_US  = 40;
const int DUST_RING_SIZE = 8;   // pulses averaged into one "reading" (smooths per-pulse noise)

// Boost converter (5V rail for dust sensor) - held on continuously once
// enabled. Confirmed via multimeter that it reaches 5V almost instantly, so
// this is just a small one-time settle margin at boot.
const unsigned int BOOST_SETTLE_MS = 50;

// Dust sensor ADC calibration -- CALIBRATE against your real hardware
const int DUST_ADC_MIN = 500;    // reading in clean air
const int DUST_ADC_MAX = 3000;   // reading at high smoke density

// ---------------------------------------------------------------------------
// Baseline calibration
// ---------------------------------------------------------------------------
const unsigned int BASELINE_WARMUP_MS    = 3000;  // let the continuous pulse train settle first
const unsigned int BASELINE_SAMPLE_COUNT = 30;    // pulses (at DUST_PULSE_CYCLE_MS) averaged into the baseline

// ---------------------------------------------------------------------------
// Smoke detection tuning
// CALIBRATE: real match-smoke test on the prior board revision measured
// delta=388 at "significant" smoke, with early signs starting around
// delta=20 above baseline. SMOKE_TRIGGER_DELTA (300) is close to that
// ceiling - worth tightening once you've done real-smoke testing on this
// board revision.
// ---------------------------------------------------------------------------
const int SMOKE_TRIGGER_DELTA    = 300;   // delta that counts as confirmed smoke
const int SMOKE_CLEAR_MARGIN     = 100;   // hysteresis: must drop back to baseline+this to "clear"
const int EARLY_WARNING_DELTA    = 20;    // delta that speeds up polling to DUST_FAST_SAMPLE_MS
const int REQUIRED_HIGH_READINGS = 3;     // consecutive highs before the alarm latches
const int REQUIRED_LOW_READINGS  = 5;     // consecutive lows before auto-clear (if not latched)

// true  = alarm stays on until "clear" is sent (serial) - real-alarm style
// false = alarm auto-clears once smoke reading sustained-drops back to baseline
const bool LATCH_ALARM = true;

// ---------------------------------------------------------------------------
// Battery voltage
// Theoretical 390k/100k divider ratio (4.9) didn't match reality on the
// prior board revision - measured against a multimeter: corrected
// multiplier was ~5.36. Re-verify against a multimeter on this board
// revision before trusting it - divider components/tolerances may differ.
// ---------------------------------------------------------------------------
const float VBAT_DIVIDER_RATIO = 5.36;

// The dust ADC reading's sensitivity to VBAT sag isn't a straight 1:1 ratio -
// on the prior board revision, comparing two real sag/recovery events, raw
// counts moved roughly 3x more (in percentage terms) than VBAT itself did.
// That fits an LDO-dropout-style nonlinearity better than simple
// proportional reference drift. Tune this exponent against real data on
// this board revision if it's over/under-correcting - see
// applyVbatCorrection() in dust_sensor.cpp.
const float VBAT_CORRECTION_EXPONENT = 3.0;

// Auto-recalibration: if VBAT drifts at least this far from the last
// calibration AND the current reading looks clean (no active alarm, delta
// below the early-warning threshold), just recalibrate against the new
// VBAT level rather than relying solely on the correction above to keep
// stretching. Never fires while fireActive or while the reading looks
// elevated - recalibrating during real smoke would erase the alarm's
// ability to see it. Cooldown prevents thrashing if VBAT hovers near the
// threshold. See handleDustSensor() in dust_sensor.cpp.
const float VBAT_RECAL_THRESHOLD    = 0.3;     // volts of drift that triggers a recalibration
const unsigned long VBAT_RECAL_COOLDOWN_MS = 60000;  // minimum time between auto-recalibrations
const float VBAT_EMPTY         = 3.3;   // volts = 0% (Table 3.2 min battery voltage)
const float VBAT_FULL          = 4.2;   // volts = 100%
const float ADC_REF_VOLTAGE    = 3.3;
const int   ADC_RESOLUTION     = 4095;  // 12-bit ADC

// ---------------------------------------------------------------------------
// 5V line sense (PIN_FIVE_V_SENSE): a 10k/10k divider off the board's 5V
// line feeds GPIO3, halving it into ADC range. Read once at boot to decide
// whether to enable the boost converter - if external 5V is already
// present (USB or wall), the battery-fed boost must not also be enabled
// onto the same rail. Also flags a low-voltage warning if 5V is present but
// weak, since a sagging supply might not meet the Sharp sensor's ~4.5V
// minimum even with the boost switched off.
//   raw pin voltage <  FIVE_V_PRESENT_THRESHOLD  -> no external 5V, boost ON
//   raw pin voltage >= FIVE_V_PRESENT_THRESHOLD  -> external 5V present, boost OFF
//   ...and if also < FIVE_V_LOW_WARNING_THRESHOLD -> flagged LOW, shown on OLED
// See dustSensorInit() in dust_sensor.cpp.
// ---------------------------------------------------------------------------
const float FIVE_V_PRESENT_THRESHOLD     = 0.5;   // raw pin volts (~1V actual rail floor)
const float FIVE_V_LOW_WARNING_THRESHOLD = 2.1;   // raw pin volts (~4.2V actual rail)

// ESP32-S3 ADC gain error - same issue VBAT_DIVIDER_RATIO corrects for.
// Multimeter measured 2.384V on GPIO3 while the raw-count-based calculation
// gave 2.26V (a factor of ~1.055 low). Re-measure and adjust if you swap
// boards, since this is chip-specific, not a fixed property of the divider.
const float FIVE_V_SENSE_CAL_RATIO = 1.055;

// The divider taps the 5V pin ahead of the Schottky diode in that path, so
// the displayed/logged rail voltage (fiveVRailVoltage in dust_sensor.cpp)
// subtracts this typical forward drop to reflect what actually reaches the
// rail after the diode, not the raw incoming voltage. A Schottky's forward
// drop isn't fixed - it rises with load current: measured ~0.2V lightly
// loaded, ~0.34V under real operating current (4.97V in -> 4.63V on the
// rail). This is a fixed mid-range approximation, not a true per-load
// model - fine since it only affects the displayed/logged estimate, not
// the boost-enable or low-voltage-warning decisions (those compare against
// the pin voltage before this subtraction).
const float SCHOTTKY_DIODE_DROP_V = 0.27;

#endif
