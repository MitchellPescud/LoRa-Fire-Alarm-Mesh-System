#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "wifi_dashboard.h"
#include "config.h"
#include "state.h"
#include "mesh.h"

static WebServer server(WEB_SERVER_PORT);

// ---------------------------------------------------------------------------
// Single-page dashboard. It polls /status once a second (one lightweight JSON
// request, so the single-threaded web server never starves the radio loop) and
// draws a card for every node heard: fire nodes that have sent a heartbeat or a
// FIRE packet. Per-node buttons act on that node's active alarm; the top
// buttons act on every active alarm.
// ---------------------------------------------------------------------------
static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
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
  .card { background:#1c1c1c; border-radius:8px; padding:14px; margin-bottom:12px; }
  .card.alarm { border:2px solid #ff4444; }
  .card.stale { border:2px solid #b8860b; }
  .head { display:flex; justify-content:space-between; align-items:center; }
  .name { font-size:1.2em; font-weight:bold; }
  .pill { font-size:0.8em; padding:3px 9px; border-radius:10px; background:#2a2a2a; color:#aaa; }
  .pill.ok { background:#173d22; color:#6e6; }
  .pill.alarm { background:#5a1414; color:#ff6b6b; }
  .pill.stale { background:#4a3a0a; color:#e0b040; }
  .pill.sil { background:#4a3a0a; color:#e0b040; margin-left:6px; }
  .meta { color:#999; font-size:0.85em; margin:6px 0 10px 0; line-height:1.5; }
  .btns { display:flex; gap:8px; }
  button { padding:11px 14px; border:none; border-radius:6px; font-size:0.95em; cursor:pointer; flex:1; color:#fff; }
  button:disabled { background:#333 !important; color:#777; cursor:not-allowed; }
  .sil { background:#cc8822; }
  .clr { background:#22aa44; }
  .global { margin-bottom:12px; }
  .label { color:#888; font-size:0.85em; }
  pre { background:#000; border-radius:4px; padding:8px; margin:8px 0 0 0; color:#8f8; font-size:0.75em;
        height:150px; overflow-y:auto; white-space:pre-wrap; }
  #msg { font-size:0.85em; color:#aaa; min-height:1.2em; margin-bottom:8px; }
</style>
</head>
<body>
<h1>Fire Command</h1>
<div class="sub" id="sub">--</div>
<div class="banner" id="banner">--</div>
<div class="global btns">
  <button class="sil" id="silAll" onclick="act('silence', 0)">Silence all</button>
  <button class="clr" id="clrAll" onclick="act('clear', 0)">Clear all alarms</button>
</div>
<div id="msg"></div>
<div id="nodes"></div>
<div class="card">
  <div class="label">Events</div>
  <pre id="log"></pre>
</div>
<div class="card">
  <div class="label">Radio / mesh counters</div>
  <div class="meta" id="stats">--</div>
</div>

<script>
let started = false;
let seenNode = {};      // id -> true once seen
let wasStale = {};      // id -> bool
let seenAlarm = {};     // "origin:episode" -> silenced?

function log(text) {
  const el = document.getElementById('log');
  el.textContent += new Date().toLocaleTimeString() + '  ' + text + '\n';
  const lines = el.textContent.split('\n');
  if (lines.length > 80) el.textContent = lines.slice(lines.length - 80).join('\n');
  el.scrollTop = el.scrollHeight;
}

function act(kind, node) {
  const what = node ? ('node ' + node) : 'ALL nodes';
  if (kind === 'clear' && !confirm('Clear the alarm on ' + what + ' for the whole network?')) return;
  const msg = document.getElementById('msg');
  msg.textContent = '';
  fetch('/' + kind + '?node=' + node, { method: 'POST' }).then(r => r.text()).then(t => {
    msg.textContent = t;
    log((kind === 'clear' ? 'CLEAR' : 'SILENCE') + ' sent for ' + what + ' - ' + t);
    refresh();
  }).catch(() => { msg.textContent = 'Request failed'; });
}

function nodeCard(id, p, alarms) {
  const inAlarm = alarms.length > 0 || (p && p.alarm);
  const stale = p && p.stale && !inAlarm;
  const allSil = alarms.length > 0 && alarms.every(a => a.silenced);
  let cls = 'card' + (inAlarm ? ' alarm' : (stale ? ' stale' : ''));
  let pill = inAlarm ? '<span class="pill alarm">FIRE</span>' : (stale ? '<span class="pill stale">UNKNOWN</span>' : '<span class="pill ok">OK</span>');
  if (allSil) pill += '<span class="pill sil">buzzer silenced</span>';
  let meta = [];
  if (p) {
    meta.push('heard ' + p.age + 's ago via ' + (p.hops === 0 ? 'direct link' : p.hops + ' hop' + (p.hops > 1 ? 's' : '')));
    meta.push('rssi ' + p.rssi + ' dBm, snr ' + p.snr + ' dB (last hop)');
    if (p.hb) {
      let h = [];
      if (p.batt <= 100) h.push('battery ' + p.batt + '%');
      h.push(p.ext ? 'on external power' : 'on battery / no 5V');
      if (p.hasTemp) h.push(p.temp.toFixed(1) + ' C');
      meta.push(h.join(' | '));
    } else {
      meta.push('no heartbeat yet');
    }
    if (stale) meta.push('no heartbeat for over 100 s - status unknown, not necessarily safe');
  } else {
    meta.push('alarm heard via the mesh; no heartbeat received from this node');
  }
  alarms.forEach(a => meta.push('alarm episode ' + a.episode + (a.silenced ? ' (silenced)' : '')));
  return '<div class="' + cls + '"><div class="head"><span class="name">Node ' + id + '</span><span>' + pill + '</span></div>'
    + '<div class="meta">' + meta.join('<br>') + '</div>'
    + '<div class="btns"><button class="sil" ' + (alarms.length ? '' : 'disabled') + ' onclick="act(\'silence\',' + id + ')">Silence</button>'
    + '<button class="clr" ' + (alarms.length ? '' : 'disabled') + ' onclick="act(\'clear\',' + id + ')">Clear alarm</button></div></div>';
}

function refresh() {
  fetch('/status').then(r => r.json()).then(d => {
    const m = d.mesh;
    document.getElementById('sub').textContent = 'Command node ' + d.addr + '  |  AP ' + d.ssid + '  |  ' + d.ip;

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

    const nodes = {};
    m.peers.forEach(p => { nodes[p.id] = { p: p, alarms: [] }; });
    m.alarms.forEach(a => { (nodes[a.origin] = nodes[a.origin] || { p: null, alarms: [] }).alarms.push(a); });
    const ids = Object.keys(nodes).map(Number).sort((a, b) => a - b);
    document.getElementById('nodes').innerHTML = ids.length
      ? ids.map(id => nodeCard(id, nodes[id].p, nodes[id].alarms)).join('')
      : '<div class="card"><div class="meta">No nodes heard yet. Fire nodes send a heartbeat about every 30 s.</div></div>';

    // event log from state changes
    ids.forEach(id => {
      const p = nodes[id].p;
      if (!seenNode[id]) { seenNode[id] = true; if (started) log('Node ' + id + ' appeared'); else log('Node ' + id + ' heard'); }
      const st = !!(p && p.stale);
      if (wasStale[id] !== undefined && wasStale[id] !== st) log('Node ' + id + (st ? ' went UNKNOWN (no heartbeat)' : ' is back'));
      wasStale[id] = st;
    });
    const cur = {};
    m.alarms.forEach(a => {
      const k = a.origin + ':' + a.episode;
      cur[k] = a.silenced;
      if (seenAlarm[k] === undefined) log('FIRE from node ' + a.origin + ' (episode ' + a.episode + ')');
      else if (!seenAlarm[k] && a.silenced) log('Alarm on node ' + a.origin + ' silenced');
    });
    Object.keys(seenAlarm).forEach(k => { if (cur[k] === undefined) log('Alarm on node ' + k.split(':')[0] + ' ended (cleared)'); });
    seenAlarm = cur;
    started = true;

    const s = m.stats;
    document.getElementById('stats').textContent = 'rx ' + s.rx + '  tx ' + s.tx + '  relayed ' + s.relayed
      + '  dup ' + s.dup + '  replay ' + s.replay + '  authFail ' + s.authFail + '  foreign ' + s.foreign
      + '  crcErr ' + s.crcErr + '  lbt ' + s.lbtDefers + '  | last rssi ' + s.rssi + ' snr ' + s.snr;
  }).catch(() => {});
}

setInterval(refresh, 1000);
refresh();
</script>
</body>
</html>
)HTML";

static void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

static void handleStatus() {
  String json = "{";
  json += "\"addr\":" + String(NODE_ADDR) + ",";
  json += "\"ssid\":\"" + String(WIFI_AP_SSID) + "\",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
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

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void wifiDashboardInit() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/silence", HTTP_POST, handleSilence);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.print("[WIFI] AP started - SSID: ");
  Serial.print(WIFI_AP_SSID);
  Serial.print("  IP: ");
  Serial.println(WiFi.softAPIP());
}

void handleWebServer() {
  server.handleClient();
}
