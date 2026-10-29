/*
  ============================================================================
  Fire Alarm Node - V6 (V5 + filtered battery reading + low power mode)
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262 + KCT8103L front-end)
  ============================================================================

  V6 adds two things on top of V5 (the mesh, radio and sensing code is untouched):
    * Battery reading is filtered (16-sample trimmed-mean bursts -> median of
      the last 9 readings -> EMA, plus a 2-point display deadband) so the
      percentage no longer jumps around by ~5%. See battery_filter.h / config.h.
    * Low power mode (power.*): lower CPU clock, loop idles 1 ms per pass, OLED
      off unless an alarm is active or the USER button / WiFi switch woke it.
      Setting off / on / auto (auto = node booted without external 5V), saved in
      flash; serial command "lowpower [on|off|auto]". The radio stays in
      continuous receive and the dust sensor keeps pulsing, so detection and
      mesh behaviour do not change.

  V4 (sensing, alarm, WiFi dashboard, LoRa mesh with no gateway) plus V5:
  ANY node can command ANY other node. A command (test fire, recalibrate
  dust baseline, reboot, status request) is an authenticated CMD packet
  addressed to one node, sent 3 times (or until its ACK arrives). The target
  runs it once and answers with an ACK that the dashboard shows as done /
  refused / no reply. The dashboard has a card for every other node with
  Silence / Clear alarm and the four commands.
  The PC-attached command node (Fire_Command_v2) is a separate, stripped
  sketch that shares the mesh code, secrets.h and the LoRa/mesh settings.

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
                          heartbeats, peer table, remote CMD/ACK, status
    node_commands.*     - what THIS node does when told to test fire /
                          recalibrate / reboot
    net_panel.h         - dashboard network cards (shared with command node)
    dust_sensor.*       - GP2Y1010AU0F pulse train, calibration, smoke eval,
                          5V-line sense -> boost enable
    alarm.*             - buzzer + alarm LED, local trigger/clear/silence
    battery.*           - filtered VBAT reading (maths in battery_filter.h)
    power.*             - low power mode (CPU clock, idle, OLED off)
    temp_sensor.*, display.*
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
    lowpower [on|off|auto]   show / set low power mode
    battery  print filtered vs instant battery voltage
    cmd <node> <fire|recal|reboot|status>   run a command on another node
  ============================================================================
*/

#include "config.h"
#include "state.h"
#include "display.h"
#include "temp_sensor.h"
#include "battery.h"
#include "power.h"
#include "dust_sensor.h"
#include "alarm.h"
#include "serial_commands.h"
#include "wifi_dashboard.h"
#include "LoRaWan_APP.h"
#include "mesh.h"
#include "node_commands.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);   // Heltec framework init (radio SPI, timers) - must precede Radio.Init()
  Serial.println();
  Serial.println("[BOOT] Fire node starting (V6: LoRa mesh + WiFi dashboard + node commands + low power mode)");
  Serial.println("[BOOT] Commands: fire | clear | silence | mesh | baseline | cmd <node> <fire|recal|reboot|status> | lowpower [on|off|auto] | battery");

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);   // power on Vext rail (OLED + BMP280) before I2C use
  delay(100);                         // let the rail settle before probing the bus

  alarmInit();
  dustSensorInit();     // 5V-sense read decides boost enable, then LED pin setup
  batteryInit();        // enables the VBAT divider and seeds the battery filter
  powerInit();          // needs usbPowerDetected from dustSensorInit(); loads saved low power setting

  tempSensorInit();     // Wire.begin() + BMP280
  displayInit();        // display.init() + splash screen

  wifiDashboardInit();  // registers HTTP routes; AP itself starts when the switch is flipped on

  meshInit();           // NVS counters, key, radio. Serviced during calibration below.

  calibrateBaseline();  // keep the area smoke-free during this
}

void loop() {
  meshService();             // radio IRQs, RX, TX queue, FIRE re-announce, heartbeats, remote commands
  nodeCommandService();      // scheduled work from remote commands (recalibrate, reboot)
  handleWifiSwitch();
  powerService();            // CPU clock follows low power mode / WiFi state, USER button wakes the OLED
  batteryService();          // filtered battery reading (own timer, independent of the OLED)
  handleSerialCommand();
  handleFireState();
  serviceDustPulseTrain();   // keeps the GP2Y1010's ~10ms pulse train running at all times
  handleDustSensor();        // decides how often to act on the current rolling average
  handleTemp();
  handleDisplay();
  handleWebServer();
  powerIdle();               // low power mode: 1 ms sleep so the core idles between passes
}
