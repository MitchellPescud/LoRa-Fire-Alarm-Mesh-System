/*
  ============================================================================
  Fire Command Node v1 (LoRa mesh + always-on WiFi dashboard)
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262 + KCT8103L front-end)
  Hardware: antenna + piezo buzzer (GPIO15). Nothing else is required.
  ============================================================================

  A stripped-down companion to the fire alarm nodes (Fire_Node_v4). It joins
  the same LoRa mesh (it hears, relays and announces itself like any node) but
  has no smoke or temperature sensing. What it adds:

    - always-on WiFi access point + dashboard: a card for every node heard
      (from heartbeats / FIRE packets) with its status, signal, battery and
      power source, and per-node Silence / Clear buttons plus Silence all /
      Clear all
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
  secrets.h are copied unchanged from Fire_Node_v4 (whose tests/ folder holds
  the PC simulator that covers them). If you change one there, copy it here.

  Open http://192.168.4.1/ after joining the "FireCommand" network.

  Serial (115200, "Newline"): clear [node] | silence [node] | mesh

  Later additions (same pins as the fire nodes): alarm LED (pick a free pin,
  NOT GPIO5 - radio PA_CTX), WiFi LED on GPIO4 and WiFi switch on GPIO16.
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

void setup() {
  Serial.begin(115200);
  delay(200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);   // Heltec framework init - must precede Radio.Init()
  Serial.println();
  Serial.println("[BOOT] Fire command node starting");
  Serial.println("[BOOT] Commands: clear [node] | silence [node] | mesh");

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);   // power the Vext rail (OLED) before using I2C
  delay(100);

  alarmInit();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  displayInit();
  wifiDashboardInit();
  meshInit();
}

void loop() {
  meshService();          // radio IRQs, RX, TX queue, heartbeats, repeated CLEAR/SILENCE
  handleSerialCommand();
  handleFireState();      // buzzer chirp while any alarm is active (unless silenced)
  handleDisplay();
  handleWebServer();
}
