#include <Arduino.h>
#include "serial_commands.h"
#include "config.h"
#include "state.h"
#include "mesh.h"
#include "wifi_dashboard.h"

// clear [node]   - CLEAR the alarm(s) of one node (or every active alarm if no node given)
// silence [node] - SILENCE the buzzer(s) likewise
// mesh           - print peers, alarms and packet counters
// cmd <node> <fire|recal|reboot|status> - run a command on another node
// wifi set <ssid> <password> - save the network to join when the WiFi switch is OFF
//                              (quote names with spaces: wifi set "My Network" "my pass";
//                               leave the password out for an open network)
// wifi reset                 - erase the saved network
// wifi status                - switch position, saved network, address

// Splits a line into words; "double quoted" text keeps its spaces. Case is preserved.
static int splitWords(const String &s, String *out, int maxWords) {
  int n = 0, i = 0, len = s.length();
  while (i < len && n < maxWords) {
    while (i < len && s.charAt(i) == ' ') i++;
    if (i >= len) break;
    String w;
    if (s.charAt(i) == '"') {
      i++;
      while (i < len && s.charAt(i) != '"') w += String(s.charAt(i++));
      if (i < len) i++;
    } else {
      while (i < len && s.charAt(i) != ' ') w += String(s.charAt(i++));
    }
    out[n++] = w;
  }
  return n;
}
void handleSerialCommand() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  String raw = line;            // original case (WiFi names and passwords are case-sensitive)
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
  } else if (cmd == "wifi") {
    String w[4];
    int n = splitWords(raw, w, 4);          // w[0]="wifi", w[1]=subcommand, w[2]=ssid, w[3]=password
    String sub = (n > 1) ? w[1] : String("");
    sub.toLowerCase();
    if (sub == "set" && n >= 3) {
      wifiSaveCredentials(w[2], n >= 4 ? w[3] : String(""));
    } else if (sub == "reset") {
      wifiResetCredentials();
    } else if (sub == "status") {
      wifiPrintStatus();
    } else {
      Serial.println("[INFO] Usage: wifi set <ssid> <password> | wifi reset | wifi status");
    }
  } else if (cmd == "cmd") {
    // cmd <node> <fire|reboot|status>  - run a command on a fire node
    // (the node number was parsed into `node` above; the command word is what follows it)
    String what = line.substring(line.indexOf(' ') + 1);
    int sp2 = what.indexOf(' ');
    String word = (sp2 > 0) ? what.substring(sp2 + 1) : "";
    word.trim();
    uint8_t code = meshCommandFromName(word.c_str());
    if (node < 1 || node > 254 || code == 0) {
      Serial.println("[INFO] Usage: cmd <node> <fire|recal|reboot|status>");
    } else {
      int r = meshSendCommand((uint8_t)node, code);
      if (r == 1) Serial.println("[INFO] That command is already pending for that node");
      else if (r < 0) Serial.println("[WARN] Cannot send (bad node, or command table full)");
    }
  } else {
    Serial.print("[INFO] Unknown command: ");
    Serial.println(cmd);
    Serial.println("[INFO] Commands: clear [node] | silence [node] | mesh | cmd <node> <fire|recal|reboot|status> | wifi set|reset|status");
  }
}
