#include <Arduino.h>
#include "serial_commands.h"
#include "config.h"
#include "state.h"
#include "alarm.h"
#include "dust_sensor.h"

// ---------------------------------------------------------------------------
// Serial commands: fire (test trigger) / clear (reset latch) / baseline
// (recalibrate). No WiFi dashboard on this board revision, so this is the
// only interface for testing/calibration.
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
