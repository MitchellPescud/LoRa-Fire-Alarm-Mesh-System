#include <Arduino.h>
#include "serial_commands.h"
#include "config.h"
#include "state.h"
#include "alarm.h"
#include "dust_sensor.h"
#include "mesh.h"
#include "power.h"

// ---------------------------------------------------------------------------
// Serial commands: fire (test trigger) / clear (reset latch) / silence (mute
// buzzers network-wide) / mesh (print mesh status) / baseline (recalibrate) /
// cmd <node> <fire|recal|reboot|status> (run a command on another node) /
// lowpower [on|off|auto] (V6: show or set low power mode) / battery (V6: raw
// vs filtered battery reading). Mirrors the actions on the web dashboard (see
// wifi_dashboard.cpp) - kept in sync manually since they're small.
// ---------------------------------------------------------------------------
void handleSerialCommand() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0) return;

  if (cmd == "fire") {
    Serial.println("[TEST] Manual fire trigger (simulating sustained smoke)");
    smokePresent = true;
    highReadingCount = 0;
    triggerAlarm();
  } else if (cmd == "clear") {
    if (!fireActive) {
      Serial.println("[INFO] No active alarm to clear");
    } else if (baselineReady && (dustRawLast - dustBaseline) > SMOKE_CLEAR_MARGIN) {
      Serial.println("[WARN] Cannot clear - dust reading is still above the clear threshold");
    } else {
      smokePresent = false;
      highReadingCount = lowReadingCount = 0;
      clearAlarm("[INFO] Alarm manually cleared");
    }
  } else if (cmd == "silence") {
    silenceAlarm();
  } else if (cmd == "mesh") {
    meshPrintStatus();
  } else if (cmd.startsWith("cmd ")) {
    // cmd <node> <fire|recal|reboot|status>  - run a command on another node
    String rest = cmd.substring(4);
    rest.trim();
    int sp = rest.indexOf(' ');
    int node = (sp > 0) ? rest.substring(0, sp).toInt() : 0;
    String what = (sp > 0) ? rest.substring(sp + 1) : "";
    what.trim();
    uint8_t code = meshCommandFromName(what.c_str());
    if (node < 1 || node > 254 || code == 0) {
      Serial.println("[INFO] Usage: cmd <node> <fire|recal|reboot|status>");
    } else {
      int r = meshSendCommand((uint8_t)node, code);
      if (r == 1) Serial.println("[INFO] That command is already pending for that node");
      else if (r < 0) Serial.println("[WARN] Cannot send (bad node, or command table full)");
    }
  } else if (cmd == "lowpower" || cmd.startsWith("lowpower ")) {
    String arg = (cmd.length() > 8) ? cmd.substring(9) : String("");
    arg.trim();
    if (arg == "on")        lowPowerSet(LP_SET_ON);
    else if (arg == "off")  lowPowerSet(LP_SET_OFF);
    else if (arg == "auto") lowPowerSet(LP_SET_AUTO);
    else if (arg.length() == 0) {
      Serial.print("[POWER] setting=");
      Serial.print(lowPowerSettingName());
      Serial.print(" active=");
      Serial.print(lowPowerActive() ? "yes" : "no");
      Serial.print(" cpu=");
      Serial.print(getCpuFrequencyMhz());
      Serial.println("MHz   (usage: lowpower on|off|auto)");
    } else {
      Serial.println("[INFO] Usage: lowpower [on|off|auto]");
    }
  } else if (cmd == "battery") {
    Serial.print("[BATT] filtered=");
    Serial.print(batteryVoltage, 3);
    Serial.print("V ");
    Serial.print(batteryPercent);
    Serial.print("%   instant=");
    Serial.print(batteryVoltageInstant, 3);
    Serial.println("V");
  } else if (cmd == "baseline") {
    if (fireActive) {
      Serial.println("[WARN] Cannot recalibrate while the alarm is active - clear it first");
    } else {
      calibrateBaseline();
    }
  } else {
    Serial.print("[INFO] Unknown command: ");
    Serial.println(cmd);
  }
}
