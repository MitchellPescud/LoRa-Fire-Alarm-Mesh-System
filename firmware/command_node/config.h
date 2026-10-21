#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Fire Command node configuration (Heltec WiFi LoRa 32 V4.3 + antenna + buzzer).
//
// The LoRa and mesh sections at the bottom are copied from the fire node's
// config.h and MUST stay identical on every node - if you change one there,
// change it here too (see "sync" note in the .ino).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Pins (same numbers as the fire nodes so the same hardware can be added later)
//
// Pins the Heltec radio driver owns on the V4.3 with the KCT8103L front-end -
// never use these for anything else: GPIO8 NSS, 9 SCK, 10 MOSI, 11 MISO,
// 12 RST, 13 BUSY, 14 DIO1, 2 CSD, 7 PA power, 5 CTX (TX/RX path select).
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA       17   // OLED
#define PIN_I2C_SCL       18
#define PIN_BUZZER        15   // active HIGH
#define PIN_VEXT_CTRL     36   // LOW = Vext (3.3V rail for the OLED) powered on

// Planned additions (not fitted yet). Same pins as the fire nodes, except the
// alarm LED which cannot be GPIO5 (radio PA_CTX) - pick a free pin such as
// GPIO38-42 when you add it, and use the same one on the fire nodes.
#define PIN_FIRE_LED      -1   // alarm LED, anode on the GPIO, HIGH = on (-1 = not fitted)
#define PIN_WIFI_LED      -1   // would be GPIO4 on the fire nodes; unused while WiFi is always on
// #define PIN_WIFI_SWITCH 16  // fire nodes: grounds the pin when ON (INPUT_PULLUP, no external pull-up)

// ---------------------------------------------------------------------------
// Node identity / WiFi access point / dashboard
// ---------------------------------------------------------------------------
#ifndef NODE_ADDR
#define NODE_ADDR        100                    // fire nodes are 1..99; command node = 100
#endif
#define NODE_ID          "command"
#define WIFI_AP_SSID     "FireCommand"
#define WIFI_AP_PASSWORD "firecommand123"       // CALIBRATE: change before real use - WPA2, 8+ chars.
                                                // Anyone on this network can clear fire alarms!
#define WEB_SERVER_PORT  80

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
const unsigned long BUZZ_INTERVAL_MS    = 1000;  // one chirp per second
const unsigned long BUZZ_ON_MS          = 150;   // chirp length
const unsigned long FIRE_LED_FLASH_MS   = 100;   // alarm LED rapid flash toggle rate (when fitted)
const unsigned long DISPLAY_REFRESH_MS  = 500;

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

#if LORA_TX_POWER_DBM > LORA_TX_POWER_MAX
#error "LORA_TX_POWER_DBM exceeds LORA_TX_POWER_MAX - check EIRP limit before raising the cap"
#endif

#endif
