/*
  ============================================================================
  Fire Command Node v2 (LoRa mesh + WiFi dashboard)
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262 + KCT8103L front-end)
  Hardware: antenna + piezo buzzer (GPIO15). Nothing else is required.
  ============================================================================

  A stripped-down companion to the fire alarm nodes (Fire_Node_v5). It joins
  the same LoRa mesh (it hears, relays and announces itself like any node) but
  has no smoke or temperature sensing. What it adds:

    - WiFi dashboard, mode set by the WiFi switch (GPIO16, grounds the pin
      when ON): ON = own access point "FireCommand"; OFF = joins the saved
      network. Save/erase that network from the serial terminal (below).
      The dashboard shows: a card for every node heard
      (from heartbeats / FIRE packets) with its status, signal, battery and
      power source, and per-node Silence / Clear buttons plus Silence all /
      Clear all
    - per-node remote commands: Test fire, Recalibrate, Reboot, Status (the
      node answers with an ACK shown on its card; Recalibrate only works on
      nodes with a dust sensor)
    - buzzer on any active alarm, from any node (silenced by SILENCE)
    - OLED status

  The dashboard's Silence / Clear buttons send authenticated SILENCE / CLEAR
  packets for the named node's active alarm. They can only act on alarms this
  node has heard (a FIRE packet received). The AP password guards them - anyone
  who joins the AP can clear alarms, so change it in config.h.

  Setup: flash with NODE_ADDR 100 (config.h). secrets.h (the pre-shared HMAC
  key) and the LoRa / mesh settings at the bottom of config.h MUST match the
  fire nodes. Arduino IDE settings are the same as the fire nodes' (Heltec
  ESP32 board package, WiFi LoRa 32(V4), Tools -> LoRa FEM = KCT8103L).

  Code sync: mesh.*, mesh_proto.*, mesh_radio.*, mesh_hmac.cpp, alarm.*,
  node_commands.h, net_panel.h, secrets.h are copied unchanged from Fire_Node_v5 (whose tests/ folder holds
  the PC simulator that covers them). If you change one there, copy it here.

  Switch ON : join the "FireCommand" network, open http://192.168.4.1/
  Switch OFF: the node joins your saved network; its address is printed on the
              serial terminal and the OLED (use `wifi status`).

  Serial (115200, "Newline"): clear [node] | silence [node] | mesh |
  cmd <node> <fire|recal|reboot|status> | wifi set <ssid> <password> |
  wifi reset | wifi status

  Hardware: WiFi switch on GPIO16 (to GND when ON). Optional later additions
  (same pins as the fire nodes): WiFi LED on GPIO4 (set PIN_WIFI_LED), alarm
  LED (pick a free pin, NOT GPIO5 - radio PA_CTX).
  ============================================================================
*/

#include <Wire.h>
#include "config.h"
#include "state.h"
#include "display.h"
#include "alarm.h"
#include "serial_commands.h"
#include "wifi_dashboard.h"
#include "LoRaWan_APP.h"
#include "mesh.h"
#include "node_commands.h"

// DIAGNOSTIC (OLED not lighting up): reports the Vext pin level and whether
// anything answers on the I2C bus. The OLED should appear as 0x3C.
static void oledProbe(const char *label) {
  Serial.print("[OLED] ");
  Serial.print(label);
  Serial.print(": Vext pin(GPIO");
  Serial.print(PIN_VEXT_CTRL);
  Serial.print(")=");
  Serial.print(digitalRead(PIN_VEXT_CTRL) == LOW ? "LOW(on)" : "HIGH(off)");
  Serial.print("  I2C devices:");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(" 0x");
      Serial.print(a, HEX);
      found++;
    }
  }
  Serial.println(found ? "" : " none");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);   // Heltec framework init - must precede Radio.Init()
  Serial.println();
  Serial.println("[BOOT] Fire command node starting");
  Serial.println("[BOOT] Commands: clear [node] | silence [node] | mesh | cmd <node> <fire|recal|reboot|status> | wifi ...");

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);   // power the Vext rail (OLED) before using I2C
  delay(100);

  alarmInit();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  oledProbe("before display init");
  displayInit();
  oledProbe("after display init");
  wifiDashboardInit();
  meshInit();
  oledProbe("after radio init");
}

void loop() {
  meshService();          // radio IRQs, RX, TX queue, heartbeats, repeated CLEAR/SILENCE, remote commands
  nodeCommandService();   // scheduled work from remote commands (reboot)
  handleSerialCommand();
  handleFireState();      // buzzer chirp while any alarm is active (unless silenced)
  handleDisplay();
  handleWifiSwitch();     // switch ON = own network, OFF = join the saved one
  handleWebServer();
}
