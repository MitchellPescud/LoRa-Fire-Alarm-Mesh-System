/*
  ============================================================================
  Fire Alarm Node - Final Board Test Build
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262)
  ============================================================================

  Stripped-down build for testing the final designed PCB. Only the
  following are populated on this board: Heltec base (OLED, VBAT divider,
  Vext), piezo buzzer, boost converter, Sharp GP2Y1010AU0F smoke sensor, and
  BMP280 temp sensor. WiFi and status-LED ("light") functionality are not
  wired on this board and are not included in this build.

  Split across multiple files - this .ino is just setup()/loop(); the actual
  logic lives in:
    config.h            - all pin assignments and tunable constants
    state.h / state.cpp - shared cross-module state (fireActive, dustBaseline, etc.)
    dust_sensor.*        - GP2Y1010AU0F continuous pulse train, calibration,
                            smoke evaluation, 5V-line sense -> boost enable
    alarm.*               - buzzer state machine, trigger/clear
    battery.*             - VBAT read -> percentage
    temp_sensor.*         - BMP280
    display.*             - OLED
    serial_commands.*    - fire / clear / baseline over Serial

  Pin changes vs. the prior (breadboard) board revision:
    PIN_BUZZER        15   (was 16)
    PIN_DUST_ADC        6  (was 3)
    PIN_FIVE_V_SENSE    3  (new - 10k/10k divider off the 5V line; same job
                            the old GPIO33 USB-sense pin did, now a real
                            voltage reading instead of just digital HIGH/LOW,
                            so a low-but-present 5V line is flagged as a
                            warning rather than silently treated as fine)
  GPIO16 is the nominal WiFi-switch position on this board but is unwired.

  No LoRa radio yet - the WiFi/dashboard features from the prior revision
  are also dropped here since this board doesn't have them wired.

  Serial commands (115200 baud, "Newline" line ending): fire (test trigger)
  / clear (reset latch) / baseline (recalibrate)
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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] Fire node starting (final board test build)");
  Serial.println("[BOOT] Commands: fire | clear | baseline");

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);   // power on Vext rail (OLED + BMP280) before I2C use
  delay(100);                         // let the rail settle before probing the bus

  alarmInit();
  dustSensorInit();     // 5V-sense read decides boost enable, then LED pin setup
  batteryInit();

  tempSensorInit();     // Wire.begin() + BMP280
  displayInit();        // display.init() + splash screen

  calibrateBaseline();  // keep the area smoke-free during this
}

void loop() {
  handleSerialCommand();
  handleFireState();
  serviceDustPulseTrain();   // keeps the GP2Y1010's ~10ms pulse train running at all times
  handleDustSensor();        // decides how often to act on the current rolling average
  handleTemp();
  handleDisplay();
}
