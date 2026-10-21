#include <Arduino.h>
#include "serial_commands.h"
#include "config.h"
#include "state.h"
#include "mesh.h"

// clear [node]   - CLEAR the alarm(s) of one node (or every active alarm if no node given)
// silence [node] - SILENCE the buzzer(s) likewise
// mesh           - print peers, alarms and packet counters
void handleSerialCommand() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;

  String cmd = line;
  int node = MESH_ALL_ORIGINS;
  int sp = line.indexOf(' ');
  if (sp > 0) {
    cmd = line.substring(0, sp);
    node = line.substring(sp + 1).toInt();
  }

  if (cmd == "clear") {
    if (meshClearOrigin((uint8_t)node) == 0) Serial.println("[INFO] No matching active alarm known");
  } else if (cmd == "silence") {
    if (meshSilenceOrigin((uint8_t)node) == 0) Serial.println("[INFO] No matching active alarm known");
  } else if (cmd == "mesh") {
    meshPrintStatus();
  } else {
    Serial.print("[INFO] Unknown command: ");
    Serial.println(cmd);
    Serial.println("[INFO] Commands: clear [node] | silence [node] | mesh");
  }
}
