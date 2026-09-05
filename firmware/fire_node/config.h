#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
// Pin assignments (Heltec WiFi LoRa 32 V4.3 / ESP32-S3) - see project
// memory / pin table for the full rationale behind each choice.
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA      17   // shared OLED + BMP280 bus
#define PIN_I2C_SCL      18
#define PIN_WIFI_SWITCH  15   // active-LOW, INPUT_PULLUP - toggles the WiFi AP on/off
#define PIN_BUZZER       16   // active HIGH
#define PIN_BOOST_EN     48   // LOW = boost converter (5V dust sensor rail) ON
#define PIN_DUST_LED     47   // GP2Y1010 LED drive (active LOW pulse)
#define PIN_DUST_ADC     3    // GP2Y1010 Vo, post 10k/20k divider + 0.1uF filter cap
#define PIN_WIFI_LED     4    // WiFi status LED
#define PIN_FIRE_LED     6    // HIGH = fire, LOW = clear
#define PIN_VBAT         1    // ADC1_CH0, battery voltage via onboard divider
#define PIN_ADC_CTRL     37   // HIGH = enable VBAT divider
#define PIN_VEXT_CTRL    36   // LOW = Vext (3.3V rail: OLED + BMP280) powered on
#define PIN_USB_SENSE    33   // 10k/10k divider off the board's 5V pin - detects USB power present
// (GPIO5 tried first for this - input reads weren't reliable despite the
// prior "output driver only" finding, moved to GPIO33: same J3 header group
// as GPIO36/37, which are already proven working as plain GPIOs on this
// board despite their alternate octal-SPI naming in the datasheet.)

// ---------------------------------------------------------------------------
// Node identity / WiFi access point / dashboard
// ---------------------------------------------------------------------------
#define NODE_ID          "node1"
#define WIFI_AP_SSID     "FireNode1"
#define WIFI_AP_PASSWORD "firealarm123"   // CALIBRATE: change before real deployment - WPA2, 8+ chars
#define WEB_SERVER_PORT  80

// ---------------------------------------------------------------------------
// General timing
// ---------------------------------------------------------------------------
const unsigned long BUZZ_INTERVAL_MS    = 1000;  // one chirp per second
const unsigned long BUZZ_ON_MS          = 150;   // chirp length
const unsigned long FIRE_LED_FLASH_MS   = 100;   // rapid flash toggle rate
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
// CALIBRATE: real match-smoke test measured delta=388 at "significant"
// smoke, with early signs starting around delta=20 above baseline.
// SMOKE_TRIGGER_DELTA (300) is close to that ceiling - worth tightening
// once you've done more real-smoke testing.
// ---------------------------------------------------------------------------
const int SMOKE_TRIGGER_DELTA    = 300;   // delta that counts as confirmed smoke
const int SMOKE_CLEAR_MARGIN     = 100;   // hysteresis: must drop back to baseline+this to "clear"
const int EARLY_WARNING_DELTA    = 20;    // delta that speeds up polling to DUST_FAST_SAMPLE_MS
const int REQUIRED_HIGH_READINGS = 3;     // consecutive highs before the alarm latches
const int REQUIRED_LOW_READINGS  = 5;     // consecutive lows before auto-clear (if not latched)

// true  = alarm stays on until "clear" is sent (serial or dashboard) - real-alarm style
// false = alarm auto-clears once smoke reading sustained-drops back to baseline
const bool LATCH_ALARM = true;

// ---------------------------------------------------------------------------
// Battery voltage
// Theoretical 390k/100k divider ratio (4.9) didn't match reality - measured
// against a multimeter on this board: corrected multiplier is ~5.36 (likely
// ESP32-S3 ADC gain error rather than the divider itself). Re-check if you
// swap boards or want to confirm it holds linearly across the battery range.
// ---------------------------------------------------------------------------
const float VBAT_DIVIDER_RATIO = 5.36;

// The dust ADC reading's sensitivity to VBAT sag isn't a straight 1:1 ratio -
// comparing two real sag/recovery events, raw counts moved roughly 3x more
// (in percentage terms) than VBAT itself did. That fits an LDO-dropout-style
// nonlinearity better than simple proportional reference drift. Tune this
// exponent against more real data if it's still over/under-correcting -
// see applyVbatCorrection() in dust_sensor.cpp.
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

#endif
