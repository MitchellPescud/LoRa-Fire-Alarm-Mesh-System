/*
  ============================================================================
  Fire Alarm Node - V4 (LoRa mesh + WiFi dashboard)
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262 + KCT8103L front-end)
  ============================================================================

  V3 (sensing, alarm, WiFi dashboard) plus the LoRa mesh: every node
  transmits, receives and relays, with no gateway. This sketch is for the
  fire alarm nodes; the PC-attached command node is a separate, stripped
  sketch that shares mesh_proto.*, secrets.h and the LoRa/mesh settings in
  config.h.

  Per-node setup (config.h): set NODE_ADDR to a unique value (1..99) before
  flashing each alarm. secrets.h (the pre-shared HMAC key) must be identical on
  every node.

  Arduino IDE (Heltec ESP32 board package): Board = WiFi LoRa 32(V4); Tools ->
  LoRa FEM must be KCT8103L (defines USE_KCT8103L_PA); USB CDC On Boot enabled;
  Flash 16MB; PSRAM OPI.

  Files:
    config.h            - pins, radio settings, mesh timing, node address
    secrets.h           - pre-shared network key (do not share)
    state.h / state.cpp - shared cross-module state
    mesh_proto.*        - wire format, HMAC tag, replay window (pure logic; PC
                          tests in tests/)
    mesh_hmac.cpp       - ESP32 HMAC-SHA256 backend (mbedtls) + key loading
    mesh_radio.*        - Heltec radio driver wrapper: RX, TX queue, LBT
    mesh.*              - flooding/relay, alarm episodes, CLEAR/SILENCE,
                          heartbeats, peer table, status
    dust_sensor.*       - GP2Y1010AU0F pulse train, calibration, smoke eval,
                          5V-line sense -> boost enable
    alarm.*             - buzzer + alarm LED, local trigger/clear/silence
    battery.*, temp_sensor.*, display.*
    serial_commands.*   - fire / clear / silence / mesh / baseline over Serial
    wifi_dashboard.*    - WiFi AP + web dashboard (WiFi switch on GPIO16)

  Alarm LED: disabled by default (PIN_FIRE_LED = -1) because GPIO5 is the
  radio's TX/RX path pin - see config.h.

  Serial commands (115200 baud, "Newline" line ending):
    fire     test trigger (starts an alarm episode and floods FIRE)
    clear    clear every active alarm, network-wide
    silence  mute buzzers on every node, alarms stay active
    mesh     print peers, active alarms and packet counters
    baseline recalibrate the dust sensor
  ============================================================================
*/

#include "config.h"
#include "state.h"
#include "display.h"
#include "temp_sensor.h"
#include "battery.h"
#include "dust_sensor.h"
#include "alarm.h"
#include "serial_commands.h"
#include "wifi_dashboard.h"
#include "LoRaWan_APP.h"
#include "mesh.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);   // Heltec framework init (radio SPI, timers) - must precede Radio.Init()
  Serial.println();
  Serial.println("[BOOT] Fire node starting (V4: LoRa mesh + WiFi dashboard)");
  Serial.println("[BOOT] Commands: fire | clear | silence | mesh | baseline");

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);   // power on Vext rail (OLED + BMP280) before I2C use
  delay(100);                         // let the rail settle before probing the bus

  alarmInit();
  dustSensorInit();     // 5V-sense read decides boost enable, then LED pin setup
  batteryInit();

  tempSensorInit();     // Wire.begin() + BMP280
  displayInit();        // display.init() + splash screen

  wifiDashboardInit();  // registers HTTP routes; AP itself starts when the switch is flipped on

  meshInit();           // NVS counters, key, radio. Serviced during calibration below.

  calibrateBaseline();  // keep the area smoke-free during this
}

void loop() {
  meshService();             // radio IRQs, RX, TX queue, FIRE re-announce, heartbeats
  handleWifiSwitch();
  handleSerialCommand();
  handleFireState();
  serviceDustPulseTrain();   // keeps the GP2Y1010's ~10ms pulse train running at all times
  handleDustSensor();        // decides how often to act on the current rolling average
  handleTemp();
  handleDisplay();
  handleWebServer();
}
