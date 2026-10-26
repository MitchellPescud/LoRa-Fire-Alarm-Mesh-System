#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "wifi_dashboard.h"
#include "config.h"
#include "state.h"
#include "mesh.h"
#include "mesh_proto.h"
#include "net_panel.h"

static WebServer server(WEB_SERVER_PORT);

// ---------------------------------------------------------------------------
// WiFi mode: chosen by the switch.  OFF -> station (join the saved network),
// ON -> our own access point.  The dashboard is served in both modes.
// ---------------------------------------------------------------------------
enum WifiMode { WM_NONE, WM_AP, WM_STA };
static WifiMode wifiMode = WM_NONE;
static bool     serverUp = false;
static String   staSsid, staPass;             // saved network (flash, else config.h defaults)
static uint32_t staLastTryMs = 0;
static bool     staWasConnected = false;
static int      switchState = -1;             // debounced: 1 = ON (grounded), 0 = OFF, -1 = not read yet
static int      switchRaw = -1;
static uint32_t switchChangedMs = 0;

static void loadCredentials() {
  staSsid = WIFI_STA_DEFAULT_SSID;
  staPass = WIFI_STA_DEFAULT_PASSWORD;
  Preferences p;
  if (p.begin("cmdwifi", false)) {
    if (p.isKey("ssid")) {
      staSsid = p.getString("ssid", "");
      staPass = p.getString("pass", "");
    }
    p.end();
  }
}

static void serverStart() { if (!serverUp) { server.begin(); serverUp = true; } }
static void serverStop()  { if (serverUp)  { server.stop();  serverUp = false; } }

static void startAccessPoint() {
  serverStop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  serverStart();
  wifiMode = WM_AP;
  Serial.print("[WIFI] switch ON: access point started - SSID: ");
  Serial.print(WIFI_AP_SSID);
  Serial.print("  IP: ");
  Serial.println(WiFi.softAPIP());
}

static void staBegin() {
  WiFi.disconnect();
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  staLastTryMs = millis();
}

static void startStation() {
  serverStop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  serverStart();
  wifiMode = WM_STA;
  staWasConnected = false;
  if (staSsid.length() == 0) {
    Serial.println("[WIFI] switch OFF: no saved network - set one with: wifi set <ssid> <password>");
    return;
  }
  Serial.print("[WIFI] switch OFF: connecting to \"");
  Serial.print(staSsid);
  Serial.println("\"");
  staBegin();
}

// Keeps the station connection going: logs changes, retries while unreachable.
static void serviceStation() {
  if (wifiMode != WM_STA || staSsid.length() == 0) return;
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected != staWasConnected) {
    staWasConnected = connected;
    if (connected) {
      Serial.print("[WIFI] connected to \"");
      Serial.print(staSsid);
      Serial.print("\"  dashboard: http://");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("[WIFI] connection lost, retrying");
    }
  }
  if (!connected && (millis() - staLastTryMs) >= WIFI_STA_RETRY_MS) staBegin();
}

void handleWifiSwitch() {
  // Debounced read: the new position must hold for 150 ms. Boot applies it at once.
  int raw = (digitalRead(PIN_WIFI_SWITCH) == LOW) ? 1 : 0;
  uint32_t now = millis();
  if (raw != switchRaw) { switchRaw = raw; switchChangedMs = now; }
  bool settled = (switchState < 0) || (now - switchChangedMs >= 150);
  if (settled && raw != switchState) {
    switchState = raw;
    if (PIN_WIFI_LED >= 0) digitalWrite(PIN_WIFI_LED, raw ? HIGH : LOW);
    if (raw) startAccessPoint(); else startStation();
  }
  serviceStation();
}

bool wifiSaveCredentials(const String &ssid, const String &password) {
  if (ssid.length() < 1 || ssid.length() > 32) { Serial.println("[WIFI] SSID must be 1..32 characters"); return false; }
  if (password.length() != 0 && (password.length() < 8 || password.length() > 63)) {
    Serial.println("[WIFI] password must be empty (open network) or 8..63 characters");
    return false;
  }
  Preferences p;
  if (!p.begin("cmdwifi", false)) { Serial.println("[WIFI] cannot open flash storage"); return false; }
  p.putString("ssid", ssid);
  p.putString("pass", password);
  p.end();
  staSsid = ssid;
  staPass = password;
  Serial.print("[WIFI] saved network \"");
  Serial.print(ssid);
  Serial.println("\"");
  if (wifiMode == WM_STA) { staWasConnected = false; staBegin(); }
  else Serial.println("[WIFI] it will be used when the WiFi switch is OFF");
  return true;
}

void wifiResetCredentials() {
  Preferences p;
  if (p.begin("cmdwifi", false)) { p.remove("ssid"); p.remove("pass"); p.end(); }
  loadCredentials();
  Serial.println("[WIFI] saved network erased (back to the config.h default)");
  if (wifiMode == WM_STA) {
    WiFi.disconnect();
    staWasConnected = false;
    if (staSsid.length()) staBegin();
    else Serial.println("[WIFI] no network saved - set one with: wifi set <ssid> <password>");
  }
}

void wifiPrintStatus() {
  Serial.print("[WIFI] switch: ");
  Serial.println(switchState == 1 ? "ON (own access point)" : "OFF (join saved network)");
  Serial.print("[WIFI] saved network: ");
  Serial.println(staSsid.length() ? staSsid : String("(none)"));
  if (wifiMode == WM_AP) {
    Serial.print("[WIFI] access point \"");
    Serial.print(WIFI_AP_SSID);
    Serial.print("\"  http://");
    Serial.println(WiFi.softAPIP());
  } else if (wifiMode == WM_STA) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WIFI] connected  http://");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(staSsid.length() ? "[WIFI] not connected (retrying)" : "[WIFI] not connected (no network saved)");
    }
  }
}

void wifiDisplayLines(String &line1, String &line2) {
  if (wifiMode == WM_AP) {
    line1 = String("WiFi: ") + WIFI_AP_SSID;
    line2 = "http://" + WiFi.softAPIP().toString();
  } else if (wifiMode == WM_STA) {
    if (staSsid.length() == 0) { line1 = "WiFi: no network set"; line2 = "serial: wifi set"; }
    else if (WiFi.status() == WL_CONNECTED) { line1 = String("WiFi: ") + staSsid; line2 = "http://" + WiFi.localIP().toString(); }
    else { line1 = String("WiFi: ") + staSsid; line2 = "connecting..."; }
  } else {
    line1 = "WiFi: starting";
    line2 = "";
  }
}

// ---------------------------------------------------------------------------
// Single-page dashboard. It polls /status once a second (one lightweight JSON
// request, so the single-threaded web server never starves the radio loop) and
// draws a card for every node heard (net_panel.h, shared with the fire nodes):
// status, signal, battery, Silence / Clear for its alarm and the remote
// commands. The top buttons act on every active alarm.
// ---------------------------------------------------------------------------
static const char PAGE_A[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Fire Command</title>
<style>
  body { font-family: sans-serif; background:#111; color:#eee; margin:0; padding:16px; max-width:760px; }
  h1 { font-size:1.3em; margin:0 0 2px 0; }
  .sub { color:#888; font-size:0.85em; margin-bottom:12px; }
  .banner { border-radius:8px; padding:14px; font-size:1.3em; font-weight:bold; margin-bottom:12px; background:#173d22; color:#6e6; }
  .banner.fire { background:#5a1414; color:#ff6b6b; }
  .global { display:flex; gap:8px; margin-bottom:12px; }
  .global button { padding:11px 14px; border:none; border-radius:6px; font-size:0.95em; cursor:pointer; flex:1; color:#fff; }
  .global button:disabled { background:#333 !important; color:#777; cursor:not-allowed; }
  .gsil { background:#cc8822; }
  .gclr { background:#22aa44; }
)HTML";

static const char PAGE_B[] PROGMEM = R"HTML(
</style>
</head>
<body>
<h1>Fire Command</h1>
<div class="sub" id="sub">--</div>
<div class="banner" id="banner">--</div>
<div class="global">
  <button class="gsil" id="silAll" onclick="netAct('silence', 0)">Silence all</button>
  <button class="gclr" id="clrAll" onclick="netAct('clear', 0)">Clear all alarms</button>
</div>
)HTML";

static const char PAGE_C[] PROGMEM = R"HTML(
<script>
)HTML";

static const char PAGE_D[] PROGMEM = R"HTML(
function refresh() {
  fetch('/status').then(r => r.json()).then(d => {
    const m = d.mesh;
    document.getElementById('sub').textContent = 'Command node ' + d.addr + '  |  ' + (d.wifiMode === 'ap' ? 'own network ' : 'network ') + d.ssid + '  |  ' + d.ip;

    const ban = document.getElementById('banner');
    if (m.alarms.length) {
      const names = [...new Set(m.alarms.map(a => a.origin))].map(o => 'node ' + o).join(', ');
      ban.textContent = 'FIRE - ' + names;
      ban.className = 'banner fire';
    } else {
      ban.textContent = 'All clear';
      ban.className = 'banner';
    }
    document.getElementById('silAll').disabled = !m.alarms.length;
    document.getElementById('clrAll').disabled = !m.alarms.length;

    netRefresh(d);
  }).catch(() => {});
}

setInterval(refresh, 1000);
refresh();
</script>
</body>
</html>
)HTML";

static void handleRoot() {
  // Sent in chunks: the page is assembled from the shared panel and this node's own parts.
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
  json += "\"addr\":" + String(NODE_ADDR) + ",";
  bool ap = (wifiMode == WM_AP);
  json += "\"wifiMode\":\"" + String(ap ? "ap" : "sta") + "\",";
  json += "\"ssid\":\"" + (ap ? String(WIFI_AP_SSID) : staSsid) + "\",";
  json += "\"ip\":\"" + (ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  json += "\"fireActive\":" + String(fireActive ? "true" : "false") + ",";
  json += meshStatusJson();
  json += "}";
  server.send(200, "application/json", json);
}

// POST /silence?node=N  and  POST /clear?node=N   (node=0 or omitted = every active alarm)
static void handleControl(bool clear) {
  long node = server.arg("node").toInt();
  if (node < 0 || node > 254) {
    server.send(400, "text/plain", "Bad node id");
    return;
  }
  int n = clear ? meshClearOrigin((uint8_t)node) : meshSilenceOrigin((uint8_t)node);
  if (n == 0) {
    server.send(200, "text/plain", "No active alarm known for that node");
    return;
  }
  String msg = String(clear ? "CLEAR" : "SILENCE") + " sent for " + String(n) + " alarm(s)";
  server.send(200, "text/plain", msg);
}

static void handleClear()   { handleControl(true); }
static void handleSilence() { handleControl(false); }

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
  else             server.send(503, "text/plain", "Cannot send right now (too many pending commands)");
}

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void wifiDashboardInit() {
  loadCredentials();
  pinMode(PIN_WIFI_SWITCH, INPUT_PULLUP);
  if (PIN_WIFI_LED >= 0) { pinMode(PIN_WIFI_LED, OUTPUT); digitalWrite(PIN_WIFI_LED, LOW); }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/silence", HTTP_POST, handleSilence);
  server.on("/cmd", HTTP_POST, handleCmd);
  server.onNotFound(handleNotFound);
  // The server starts (and WiFi mode is chosen) in handleWifiSwitch().
}

void handleWebServer() {
  if (serverUp) server.handleClient();
}
