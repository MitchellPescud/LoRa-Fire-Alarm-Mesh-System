#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Pin assignments - V3/V4 board revision (Heltec WiFi LoRa 32 V4.3 / ESP32-S3).
// This is the standard pin config for all future boards.
//
// Pins the Heltec radio driver owns on the V4.3 with the KCT8103L front-end
// (do not use for anything else): GPIO8 NSS, 9 SCK, 10 MOSI, 11 MISO, 12 RST,
// 13 BUSY, 14 DIO1, 2 CSD (FEM enable), 7 PA power, and 5 CTX (TX/RX path
// select - the driver writes it on every TX/RX switch).
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA       17   // shared OLED + BMP280 bus
#define PIN_I2C_SCL       18
#define PIN_BUZZER        15   // active HIGH
#define PIN_FIRE_LED      -1   // alarm LED - anode on the GPIO, HIGH = on. DISABLED (-1) in V4:
                               // GPIO5 is the radio's PA_CTX pin (TX/RX switch) and the LoRa
                               // driver drives it constantly, so an LED there both glitches and
                               // can put the receiver on the wrong RF path. Move the LED to a
                               // free pin (GPIO38-42 are unused on this board) and set it here.
#define PIN_WIFI_LED      4    // WiFi status LED - anode on the GPIO, HIGH = on
#define PIN_WIFI_SWITCH   16   // grounds the pin when ON (active-LOW). NO external pull-up -
                               // firmware must use INPUT_PULLUP (see wifiDashboardInit()).
#define PIN_BOOST_EN      48   // LOW = boost converter (5V dust sensor rail) ON
#define PIN_DUST_LED      47   // GP2Y1010 LED drive (active LOW pulse)
#define PIN_DUST_ADC      6    // GP2Y1010 Vo (moved from GPIO3 on the prior board revision)
#define PIN_FIVE_V_SENSE  3    // 10k/10k divider off the board's 5V line - decides boost enable
#define PIN_VBAT          1    // ADC1_CH0, battery voltage via onboard divider
#define PIN_ADC_CTRL      37   // HIGH = enable VBAT divider
#define PIN_VEXT_CTRL     36   // LOW = Vext (3.3V rail: OLED + BMP280) powered on
#define PIN_USER_BTN      0    // board's PRG/USER button (active LOW, board has its own pull-up). Low power mode:
                               // press to wake the OLED. -1 disables. GPIO0 is a boot-strapping pin, but only
                               // while the chip resets - it is a normal input afterwards.

// ---------------------------------------------------------------------------
// Node identity / WiFi access point / dashboard
// ---------------------------------------------------------------------------
#define FN_XSTR(x) #x
#define FN_STR(x)  FN_XSTR(x)
#ifndef NODE_ADDR
#define NODE_ADDR        1                      // UNIQUE per fire node (1..99). Command node = 100.
#endif
#define NODE_ID          "node" FN_STR(NODE_ADDR)
#define WIFI_AP_SSID     "FireNode" FN_STR(NODE_ADDR)
#define WIFI_AP_PASSWORD "firealarm123"   // CALIBRATE: change before real deployment - WPA2, 8+ chars
#define WEB_SERVER_PORT  80

// ---------------------------------------------------------------------------
// General timing
// ---------------------------------------------------------------------------
const unsigned long BUZZ_INTERVAL_MS    = 1000;  // one chirp per second
const unsigned long BUZZ_ON_MS          = 150;   // chirp length
const unsigned long FIRE_LED_FLASH_MS   = 100;   // alarm LED rapid flash toggle rate
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
// Battery reading filter (V6) - see battery_filter.h for what each stage fixes.
// A single analogRead() through the 390k/100k divider is multiplied by ~5.36,
// so a few counts of ADC noise already shows up as several percent, and the
// LoRa PA / WiFi pull enough current (datasheet: ~290 mA at 14 dBm TX) to sag
// the cell by tens of millivolts for the length of a packet.
//   reading  = BATT_BURST_SAMPLES samples, BATT_BURST_TRIM dropped at each end
//   filtered = median of the last BATT_MEDIAN_WINDOW readings, then EMA
// Defaults: a reading every 500 ms, window 9 -> any disturbance shorter than
// ~2 s is rejected; EMA 0.25 -> ~2 s extra smoothing; whole thing settles in
// roughly 5-8 s, far faster than a cell actually drains.
// ---------------------------------------------------------------------------
const int           BATT_BURST_SAMPLES     = 16;
const int           BATT_BURST_TRIM        = 3;      // drop this many lowest AND highest
const unsigned int  BATT_BURST_GAP_US      = 100;
const unsigned long BATT_SAMPLE_MS         = 500;    // one reading per this
const int           BATT_MEDIAN_WINDOW     = 9;      // readings (odd number; max 31)
const float         BATT_EMA_ALPHA         = 0.25f;  // weight of each new median (smaller = smoother)
const int           BATT_PCT_DEADBAND      = 2;      // displayed % moves only when it changes by at least this
const int           BATT_SEED_BURSTS       = 4;      // bursts averaged at boot to seed the filter
const unsigned int  BATT_DIVIDER_SETTLE_MS = 20;     // after enabling ADC_CTRL, before the first read

// ---------------------------------------------------------------------------
// Low power mode (V6) - see power.cpp. Applies when the node runs from its
// battery. Setting: OFF / ON / AUTO, saved in flash, changed with the serial
// command "lowpower [on|off|auto]". AUTO = on whenever the node booted
// without external 5V (same boot-time check that decides the boost converter).
// What it changes - and only this; mesh, radio and smoke sensing run exactly
// as before, so every alert is still received, relayed and detected:
//   * CPU clock 240 -> LP_CPU_MHZ (LP_CPU_MHZ_WIFI while the WiFi AP is up;
//     the WiFi driver needs >= 80 MHz)
//   * loop() sleeps 1 ms per pass so the core idles instead of spinning
//   * OLED switched off; wakes on any alarm, the USER button, or the WiFi switch
// ---------------------------------------------------------------------------
#define LOWPOWER_DEFAULT_SETTING 2                    // 0 = OFF, 1 = ON, 2 = AUTO
const uint32_t      LP_CPU_MHZ             = 80;      // CPU clock while in low power mode (80 / 160 / 240)
const uint32_t      LP_CPU_MHZ_WIFI        = 160;     // ... while the WiFi AP is running
const unsigned long LP_DISPLAY_WAKE_MS     = 15000;   // how long the OLED stays on after a wake
const unsigned long LP_BUTTON_DEBOUNCE_MS  = 50;

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

// ---------------------------------------------------------------------------
// LoRa radio (Heltec framework, SX1262 + KCT8103L front-end). All nodes,
// including the command node, must use identical values here.
// ---------------------------------------------------------------------------
#define LORA_FREQ_HZ        921000000UL   // custom channel inside AU915 (915-928 MHz), away from LoRaWAN/Meshtastic defaults
#define LORA_BW_INDEX       0             // 0 = 125 kHz
#define LORA_SF             9             // spreading factor (SF9 ~ 190 ms airtime for these packets)
#define LORA_CR_INDEX       1             // 1 = 4/5
#define LORA_PREAMBLE_LEN   8
#define LORA_SYNC_WORD      0x5A          // PHY-level network filter (NOT security). Avoid 0x12 (private) and 0x34 (LoRaWAN public)

// SX1262 output setting; the KCT8103L adds roughly +6 dB, so 22 dBm here is
// about 28 dBm at the antenna connector. Limit is 30 dBm EIRP (AU915) and
// that INCLUDES antenna gain. 10 is a safe bench value; raise it for
// deployment only after working out your antenna's gain. Never transmit
// without an antenna attached.
#define LORA_TX_POWER_DBM   10
#define LORA_TX_POWER_MAX   20            // hard cap, enforced at compile time below

// Listen-before-talk: defer a transmit if the channel RSSI is above this.
const int16_t  LBT_RSSI_DBM          = -95;
const uint8_t  LBT_MAX_DEFERS        = 6;
const uint16_t LBT_BACKOFF_MIN_MS    = 30;
const uint16_t LBT_BACKOFF_MAX_MS    = 150;

// ---------------------------------------------------------------------------
// Mesh behaviour
// ---------------------------------------------------------------------------
const unsigned long FIRE_REANNOUNCE_MS = 5000;    // origin repeats FIRE this often while its episode is active
// Re-announce / repeat timers get random jitter so two nodes that cannot hear
// each other (hidden nodes) don't collide at a shared neighbour every time.
const unsigned long FIRE_JITTER_MS     = 750;     // FIRE_REANNOUNCE_MS +/- this
const unsigned long CTRL_JITTER_MS     = 400;     // CTRL_REPEAT_MS +/- this
const unsigned long HB_INTERVAL_MS     = 30000;   // heartbeat period (suppressed while any alarm is active)
const unsigned long PEER_STALE_MS      = 100000;  // no heartbeat for this long -> peer shown as UNKNOWN
const unsigned long CTRL_REPEAT_MS     = 1500;    // CLEAR / SILENCE are sent several times for reliability
const uint8_t       CTRL_REPEATS       = 3;
const unsigned long REARM_HOLDOFF_MS   = 60000;   // after our own episode is cleared, no new local episode for this long

const uint8_t  FIRE_TTL  = 5;
const uint8_t  CTRL_TTL  = 5;
const uint8_t  HB_TTL    = 3;

// Relay timing: a relay waits longer the stronger the packet was heard
// (near nodes back off, far nodes go first - pushes the flood outward) plus
// random jitter. Spread must exceed one packet's airtime (~190 ms) to avoid
// relays colliding.
const uint16_t RELAY_MAX_DELAY_MS = 600;
const uint16_t RELAY_JITTER_MS    = 120;
const int16_t  RELAY_RSSI_FAR     = -115;   // heard this weakly  -> relay immediately
const int16_t  RELAY_RSSI_NEAR    = -40;    // heard this strongly -> relay after RELAY_MAX_DELAY_MS

#define MESH_MAX_PEERS   16
#define MESH_MAX_ALARMS  4
#define MESH_MAX_CTRL    6
#define MESH_TX_QUEUE    8
#define MESH_MAX_OUT     8                        // remote commands this node is tracking (pending + recent)

// Remote commands (CMD / ACK). CMD is sent CTRL_REPEATS times unless the
// target's ACK arrives first; with no ACK the request is shown as "no reply"
// this long after the last copy went out.
const unsigned long CMD_ACK_WAIT_MS    = 8000;

#if LORA_TX_POWER_DBM > LORA_TX_POWER_MAX
#error "LORA_TX_POWER_DBM exceeds LORA_TX_POWER_MAX - check EIRP limit before raising the cap"
#endif

#endif
