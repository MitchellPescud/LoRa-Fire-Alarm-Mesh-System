/*
  ============================================================================
  Fire Alarm Node - V3 (WiFi + dashboard, no LoRa yet)
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262)
  ============================================================================

  V3 build for the new boards: V2's final-board sensing (5V-line sense,
  Sharp GP2Y1010AU0F, BMP280, buzzer, boost converter) plus the WiFi soft-AP
  dashboard, WiFi switch/LED and flashing alarm LED. LoRa mesh networking is
  deliberately NOT included yet.

  Split across multiple files - this .ino is just setup()/loop(); the actual
  logic lives in:
    config.h            - all pin assignments and tunable constants
    state.h / state.cpp - shared cross-module state
    dust_sensor.*       - GP2Y1010AU0F pulse train, calibration, smoke
                          evaluation, 5V-line sense -> boost enable
    alarm.*             - buzzer + alarm LED state machine, trigger/clear
    battery.*           - VBAT read -> percentage
    temp_sensor.*       - BMP280
    display.*           - OLED
    serial_commands.*   - fire / clear / baseline over Serial
    wifi_dashboard.*    - WiFi AP + web dashboard (served when the WiFi switch is on)

  V3 pins (see config.h): GPIO4 WiFi LED, GPIO5 alarm LED (both anode on the
  GPIO), GPIO15 buzzer, GPIO16 WiFi switch (grounds when ON, INPUT_PULLUP),
  GPIO3 5V sense, GPIO6 dust ADC.

  Serial commands (115200 baud, "Newline" line ending) and dashboard both
  support: fire (test trigger) / clear (reset latch) / baseline (recalibrate)

  WiFi: flip the WiFi switch on to bring up the "FireNode1" access point
  (see config.h for SSID/password) and open http://<AP IP>/ - the IP is
  printed to Serial when the AP starts (defaults to 192.168.4.1).
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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] Fire node starting (V3: WiFi + dashboard, no LoRa yet)");
  Serial.println("[BOOT] Commands: fire | clear | baseline");

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);   // power on Vext rail (OLED + BMP280) before I2C use
  delay(100);                         // let the rail settle before probing the bus

  alarmInit();
  dustSensorInit();     // 5V-sense read decides boost enable, then LED pin setup
  batteryInit();

  tempSensorInit();     // Wire.begin() + BMP280
  displayInit();        // display.init() + splash screen

  wifiDashboardInit();  // registers HTTP routes; AP itself starts when the switch is flipped on

  calibrateBaseline();  // keep the area smoke-free during this
}

void loop() {
  handleWifiSwitch();
  handleSerialCommand();
  handleFireState();
  serviceDustPulseTrain();   // keeps the GP2Y1010's ~10ms pulse train running at all times
  handleDustSensor();        // decides how often to act on the current rolling average
  handleTemp();
  handleDisplay();
  handleWebServer();
}
