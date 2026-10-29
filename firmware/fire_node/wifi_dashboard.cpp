#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "wifi_dashboard.h"
#include "config.h"
#include "state.h"
#include "alarm.h"
#include "dust_sensor.h"
#include "mesh.h"
#include "mesh_proto.h"
#include "net_panel.h"
#include "power.h"

static WebServer server(WEB_SERVER_PORT);
static bool lastSwitchOn = false;   // for edge-detection, so we only start/stop the AP on a change

// ---------------------------------------------------------------------------
// Single-page dashboard: fetch()/setInterval polls /status every second
// instead of reloading the whole page - keeps the ESP32's single-threaded
// WebServer responsive (per project notes: minimize request overhead with a
// single lightweight polling endpoint, especially since it may end up
// competing with LoRa radio interrupts later).
// ---------------------------------------------------------------------------
static const char PAGE_A[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fire Node Dashboard</title>
<style>
  body { font-family: sans-serif; background:#111; color:#eee; margin:0; padding:20px; }
  h1 { font-size: 1.3em; margin-bottom: 4px; }
  .node-id { color:#888; font-size:0.85em; margin-bottom:16px; }
  .card { background:#1c1c1c; border-radius:8px; padding:16px; margin-bottom:12px; }
  .label { color:#888; font-size:0.85em; }
  .value { font-size:1.6em; font-weight:bold; margin-top:4px; }
  .fire { color:#ff4444; }
  .normal { color:#44dd77; }
  button { padding:12px 20px; border:none; border-radius:6px; font-size:1em; cursor:pointer; width:100%; }
  .clear-btn { background:#22cc44; color:#fff; }
  .clear-btn:disabled { background:#444; color:#888; cursor:not-allowed; }
  .fire-btn { background:#cc3322; color:#fff; margin-bottom:8px; }
  .fire-btn:disabled { background:#444; color:#888; cursor:not-allowed; }
  .sil-btn { background:#cc8822; color:#fff; margin-bottom:8px; }
  .sil-btn:disabled { background:#444; color:#888; cursor:not-allowed; }
  .cal-btn { background:#2266cc; color:#fff; margin-bottom:8px; }
  .cal-btn:disabled { background:#444; color:#888; cursor:not-allowed; }
  #msg { font-size:0.85em; color:#aaa; margin-top:8px; min-height: 1.2em; }
  .log-box { background:#000; border-radius:4px; padding:8px; margin-top:8px;
    font-family: monospace; font-size:0.75em; color:#8f8; height:160px;
    overflow-y:auto; white-space:pre-wrap; }
)HTML";

static const char PAGE_B[] PROGMEM = R"HTML(
</style>
</head>
<body>
<h1>Fire Node Dashboard</h1>
<div class="node-id" id="nodeId">--</div>

<div class="card">
  <div class="label">Status</div>
  <div class="value" id="status">--</div>
</div>
<div class="card">
  <div class="label">Temperature</div>
  <div class="value" id="temp">--</div>
</div>
<div class="card">
  <div class="label">Battery</div>
  <div class="value" id="batt">--</div>
</div>
<div class="card">
  <div class="label">Power source</div>
  <div class="value" id="power">--</div>
</div>
<div class="card">
  <div class="label">Smoke (raw / delta above baseline)</div>
  <div class="value" id="smoke">--</div>
</div>
<div class="card">
  <button class="fire-btn" id="fireBtn" onclick="testFire()">Test Fire Alarm</button>
  <button class="sil-btn" id="silBtn" onclick="silenceAlarm()">Silence Buzzers (all nodes)</button>
  <button class="cal-btn" id="calBtn" onclick="recalibrate()">Recalibrate Baseline</button>
  <button class="clear-btn" id="clearBtn" onclick="clearAlarm()">Clear Alarm</button>
  <div id="msg"></div>
  <div id="log" class="log-box"></div>
</div>
<h1 style="margin-top:24px">Network</h1>
)HTML";

static const char PAGE_C[] PROGMEM = R"HTML(
<script>
)HTML";

static const char PAGE_D[] PROGMEM = R"HTML(
let lastKnownBaseline = null;   // detects any recalibration (manual, serial, or auto) between polls

function refresh() {
  fetch('/status').then(r => r.json()).then(d => {
    document.getElementById('nodeId').textContent = 'Node: ' + d.nodeId;

    const statusEl = document.getElementById('status');
    statusEl.textContent = d.fireActive ? 'FIRE' : 'Normal';
    statusEl.className = 'value ' + (d.fireActive ? 'fire' : 'normal');

    document.getElementById('temp').textContent = d.hasTemp ? (d.tempC.toFixed(1) + ' C') : 'N/A';
    document.getElementById('batt').textContent = d.battPercent + '% (' + d.battVoltage.toFixed(2) + 'V)';
    document.getElementById('power').textContent = d.extPower
      ? ('External 5V ' + d.fiveV.toFixed(2) + 'V' + (d.fiveVLow ? ' (LOW!)' : ''))
      : 'Battery (boost)';
    if (d.lowPower) document.getElementById('power').textContent += ' - low power mode';
    document.getElementById('smoke').textContent = d.dustRaw + ' / ' + d.dustDelta
      + (d.baselineReady ? '' : ' (calibrating...)');

    if (d.mesh) netRefresh(d);   // node cards + events + counters (net_panel.h)
    document.getElementById('silBtn').disabled = !d.fireActive;
    document.getElementById('clearBtn').disabled = !d.fireActive;
    document.getElementById('fireBtn').disabled = d.fireActive;
    document.getElementById('calBtn').disabled = d.fireActive;

    const logEl = document.getElementById('log');
    if (lastKnownBaseline !== null && d.dustBaseline !== lastKnownBaseline) {
      logEl.textContent += '>>> RECALIBRATED: baseline ' + lastKnownBaseline + ' -> ' + d.dustBaseline + ' <<<\n';
    }
    lastKnownBaseline = d.dustBaseline;

    const ts = new Date().toLocaleTimeString();
    logEl.textContent += ts + '  raw=' + d.dustRaw + ' baseline=' + d.dustBaseline
      + ' delta=' + d.dustDelta + ' batt=' + d.battVoltage.toFixed(2) + 'V\n';
    const lines = logEl.textContent.split('\n');
    if (lines.length > 50) logEl.textContent = lines.slice(lines.length - 50).join('\n');
    logEl.scrollTop = logEl.scrollHeight;
  }).catch(() => {});
}

function testFire() {
  if (!confirm('Trigger a test fire alarm? This sounds the buzzer and flashes the fire LED.')) return;
  const msg = document.getElementById('msg');
  msg.textContent = '';
  fetch('/fire', { method: 'POST' }).then(r => r.text()).then(t => {
    msg.textContent = t;
    refresh();
  });
}

function silenceAlarm() {
  const msg = document.getElementById('msg');
  msg.textContent = '';
  fetch('/silence', { method: 'POST' }).then(r => r.text()).then(t => {
    msg.textContent = t;
    refresh();
  });
}

function recalibrate() {
  if (!confirm('Recalibrate baseline? Keep the area smoke-free - this takes a few seconds and the page will pause while it runs.')) return;
  const msg = document.getElementById('msg');
  msg.textContent = 'Recalibrating...';
  fetch('/baseline', { method: 'POST' }).then(r => r.text()).then(t => {
    msg.textContent = t;
    refresh();
  });
}

function clearAlarm() {
  const msg = document.getElementById('msg');
  msg.textContent = '';
  fetch('/clear', { method: 'POST' }).then(r => r.text()).then(t => {
    msg.textContent = t;
    refresh();
  });
}

setInterval(refresh, 1000);
refresh();
</script>
</body>
</html>
)HTML";

static void handleRoot() {
  // Sent in chunks: the page is assembled from this node's parts and the shared network panel.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(PAGE_A);
  server.sendContent_P(NET_PANEL_CSS);
  server.sendContent_P(PAGE_B);
  server.sendContent_P(NET_PANEL_HTML);
  server.sendContent_P(PAGE_C);
  server.sendContent_P(NET_PANEL_JS);
  server.sendContent_P(PAGE_D);
  server.sendContent("");
}

static void handleStatus() {
  String json = "{";
  json += "\"nodeId\":\"" + String(NODE_ID) + "\",";
  json += "\"fireActive\":" + String(fireActive ? "true" : "false") + ",";
  json += "\"hasTemp\":" + String(bmpOk ? "true" : "false") + ",";
  json += "\"tempC\":" + String(bmpOk ? lastTemp : 0.0f, 1) + ",";
  json += "\"battPercent\":" + String(batteryPercent) + ",";
  json += "\"battVoltage\":" + String(batteryVoltage, 2) + ",";
  json += "\"lowPower\":" + String(lowPowerActive() ? "true" : "false") + ",";
  json += "\"dustRaw\":" + String(dustRawLast) + ",";
  json += "\"dustBaseline\":" + String(dustBaseline) + ",";
  json += "\"dustDelta\":" + String(dustRawLast - dustBaseline) + ",";
  json += "\"extPower\":" + String(usbPowerDetected ? "true" : "false") + ",";
  json += "\"fiveVLow\":" + String(usbVoltageLow ? "true" : "false") + ",";
  json += "\"fiveV\":" + String(fiveVRailVoltage, 2) + ",";
  json += "\"baselineReady\":" + String(baselineReady ? "true" : "false") + ",";
  json += meshStatusJson();
  json += "}";
  server.send(200, "application/json", json);
}

// Mirrors the "fire" serial command - manually trigger the alarm as if
// sustained smoke were confirmed, for testing without an actual smoke
// source. See serial_commands.cpp.
static void handleFire() {
  Serial.println("[TEST] Manual fire trigger via dashboard");
  smokePresent = true;
  highReadingCount = 0;
  triggerAlarm();
  server.send(200, "text/plain", fireActive ? "Fire alarm triggered" : "Already active");
}

// Mirrors the "silence" serial command - mutes buzzers on every node.
static void handleSilence() {
  long node = server.arg("node").toInt();
  if (node > 0 && node != NODE_ADDR && node < 255) {   // a card for ANOTHER node on the network panel
    int n = meshSilenceOrigin((uint8_t)node);
    server.send(200, "text/plain", n ? "SILENCE sent for that node's alarm" : "No active alarm known for that node");
    return;
  }
  if (!fireActive) {
    server.send(200, "text/plain", "No active alarm");
    return;
  }
  Serial.println("[TEST] Silence via dashboard");
  silenceAlarm();
  server.send(200, "text/plain", "Silence sent to all nodes");
}

// Mirrors the "baseline" serial command. Blocking (~3.3s) - the HTTP
// request just waits it out; see calibrateBaseline() in dust_sensor.cpp.
static void handleBaseline() {
  if (fireActive) {
    server.send(409, "text/plain", "Cannot recalibrate while the alarm is active - clear it first");
    return;
  }
  Serial.println("[TEST] Manual baseline recalibration via dashboard");
  calibrateBaseline();
  server.send(200, "text/plain", "Baseline recalibrated");
}

// Same clear rules as the "clear" serial command: refuse if the sensor is
// still reading elevated - see serial_commands.cpp.
static void handleClear() {
  long node = server.arg("node").toInt();
  if (node > 0 && node != NODE_ADDR && node < 255) {   // a card for ANOTHER node on the network panel
    int n = meshClearOrigin((uint8_t)node);
    server.send(200, "text/plain", n ? "CLEAR sent for that node's alarm" : "No active alarm known for that node");
    return;
  }
  if (!fireActive) {
    server.send(200, "text/plain", "No active alarm");
    return;
  }
  if (baselineReady && (dustRawLast - dustBaseline) > SMOKE_CLEAR_MARGIN) {
    server.send(409, "text/plain", "Still elevated - cannot clear yet");
    return;
  }
  smokePresent = false;
  highReadingCount = lowReadingCount = 0;
  clearAlarm("[INFO] Alarm cleared via dashboard");
  server.send(200, "text/plain", "Cleared");
}

// POST /cmd?node=N&cmd=C   (C = MESH_CMD_*: 1 test fire, 2 recalibrate, 3 reboot, 4 status)
static void handleCmd() {
  long node = server.arg("node").toInt();
  long cmd = server.arg("cmd").toInt();
  if (node < 1 || node > 254 || cmd < 1 || cmd > MESH_CMD_MAX) {
    server.send(400, "text/plain", "Bad node or command");
    return;
  }
  int r = meshSendCommand((uint8_t)node, (uint8_t)cmd);
  if (r == 0)      server.send(200, "text/plain", "Sent - waiting for node " + String(node) + " to reply");
  else if (r == 1) server.send(200, "text/plain", "That command is already pending for node " + String(node));
  else if (r == -1) server.send(400, "text/plain", "Cannot send that command to this node");
  else             server.send(503, "text/plain", "Cannot send right now (too many pending commands)");
}

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void wifiDashboardInit() {
  pinMode(PIN_WIFI_SWITCH, INPUT_PULLUP);
  pinMode(PIN_WIFI_LED, OUTPUT);
  digitalWrite(PIN_WIFI_LED, LOW);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/fire", HTTP_POST, handleFire);
  server.on("/baseline", HTTP_POST, handleBaseline);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/silence", HTTP_POST, handleSilence);
  server.on("/cmd", HTTP_POST, handleCmd);
  server.onNotFound(handleNotFound);
}

static void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  server.begin();
  wifiApActive = true;

  Serial.print("[WIFI] AP started - SSID: ");
  Serial.print(WIFI_AP_SSID);
  Serial.print("  IP: ");
  Serial.println(WiFi.softAPIP());
}

static void stopAccessPoint() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiApActive = false;
  Serial.println("[WIFI] AP stopped");
}

void handleWifiSwitch() {
  bool switchOn = (digitalRead(PIN_WIFI_SWITCH) == LOW);  // active-low
  digitalWrite(PIN_WIFI_LED, switchOn ? HIGH : LOW);

  if (switchOn == lastSwitchOn) return;  // only act on a change, not every loop() pass
  lastSwitchOn = switchOn;

  if (switchOn) {
    startAccessPoint();
  } else {
    stopAccessPoint();
  }
}

void handleWebServer() {
  if (wifiApActive) {
    server.handleClient();
  }
}
