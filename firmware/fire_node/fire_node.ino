/*
  ============================================================================
  Fire Alarm Mesh Node
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262)
  ============================================================================

  Split across multiple files - this .ino is just setup()/loop(); the actual
  logic lives in:
    config.h            - all pin assignments and tunable constants
    state.h / state.cpp - shared cross-module state (fireActive, dustBaseline, etc.)
    dust_sensor.*        - GP2Y1010AU0F continuous pulse train, calibration, smoke evaluation
    alarm.*               - buzzer/fire LED state machine, trigger/clear
    battery.*             - VBAT read -> percentage
    temp_sensor.*         - BMP280
    display.*             - OLED
    wifi_dashboard.*      - WiFi AP + web dashboard (served when the WiFi switch is on)
    serial_commands.*    - fire / clear / baseline over Serial

  Not included yet: LoRa mesh radio (Heltec's LoRaWan_APP.h / RadioEvents) -
  this build is WiFi + dashboard only, per the current scope. The `display`
  global still comes from Heltec's board package (compiled in automatically
  via HT_SSD1306Wire.h), so adding LoRa later shouldn't require touching the
  display wiring.

  Serial commands (115200 baud, "Newline" line ending) and dashboard both
  support: fire (test trigger) / clear (reset latch) / [baseline - serial only]

  WiFi: flip the WiFi toggle switch on to bring up the "FireNode1" access
  point (see config.h for SSID/password) and open http://<AP IP>/ - the IP
  is printed to Serial when the AP starts (defaults to 192.168.4.1).
  ============================================================================
*/

#include "config.h"
#include "state.h"
#include "display.h"
#include "temp_sensor.h"
#include "battery.h"
#include "dust_sensor.h"
#include "alarm.h"
#include "wifi_dashboard.h"
#include "serial_commands.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] Fire node starting (WiFi + dashboard, no LoRa yet)");
  Serial.println("[BOOT] Commands: fire | clear | baseline");

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);   // power on Vext rail (OLED + BMP280) before I2C use
  delay(100);                         // let the rail settle before probing the bus

  alarmInit();
  dustSensorInit();     // boost converter on + settle, dust LED pin
  batteryInit();
  wifiDashboardInit();  // registers HTTP routes; AP itself starts when the switch is flipped on

  tempSensorInit();     // Wire.begin() + BMP280
  displayInit();        // display.init() + splash screen

  calibrateBaseline();  // keep the area smoke-free during this
}

void loop() {
  handleWifiSwitch();
  // handleUsbSense();       // disabled - some USB sources (e.g. a PC port) can't supply enough
                              // current for the dust sensor rail alone, so switching the boost off
                              // mid-run on those sources could brown out the 5V rail. Boot-time
                              // detection in dustSensorInit() is unaffected. Re-enable once a safer
                              // check (e.g. verifying the 5V rail holds up) is in place.
  handleSerialCommand();
  handleFireState();
  serviceDustPulseTrain();   // keeps the GP2Y1010's ~10ms pulse train running at all times
  handleDustSensor();        // decides how often to act on the current rolling average
  handleTemp();
  handleDisplay();
  handleWebServer();
}
