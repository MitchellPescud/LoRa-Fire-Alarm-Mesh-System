#ifndef NET_PANEL_H
#define NET_PANEL_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// "Network" section of the dashboards: one card per other node (status, signal,
// battery, Silence / Clear for its alarm, and the remote commands Test fire /
// Recalibrate / Reboot / Status), plus an event log and radio counters.
// Shared by the fire node page and the command node page; each page sends
//   its own head+CSS, NET_PANEL_CSS, its own body, NET_PANEL_HTML, <script>,
//   NET_PANEL_JS, its own script, </script>
// and calls netRefresh(statusJson) from its once-a-second poll.
//
// Page requirements: the server must implement
//   POST /silence?node=N   POST /clear?node=N   POST /cmd?node=N&cmd=C
// ---------------------------------------------------------------------------

static const char NET_PANEL_CSS[] PROGMEM = R"HTML(
  #net .card { background:#1c1c1c; border-radius:8px; padding:14px; margin-bottom:12px; }
  #net .card.alarm { border:2px solid #ff4444; }
  #net .card.stale { border:2px solid #b8860b; }
  #net .head { display:flex; justify-content:space-between; align-items:center; }
  #net .name { font-size:1.2em; font-weight:bold; }
  #net .pill { font-size:0.8em; padding:3px 9px; border-radius:10px; background:#2a2a2a; color:#aaa; }
  #net .pill.ok { background:#173d22; color:#6e6; }
  #net .pill.alarm { background:#5a1414; color:#ff6b6b; }
  #net .pill.stale { background:#4a3a0a; color:#e0b040; }
  #net .pill.sil { background:#4a3a0a; color:#e0b040; margin-left:6px; }
  #net .meta { color:#999; font-size:0.85em; margin:6px 0 10px 0; line-height:1.5; }
  #net .btns { display:flex; gap:8px; margin-bottom:8px; }
  #net .btns:last-child { margin-bottom:0; }
  #net button { padding:10px 8px; border:none; border-radius:6px; font-size:0.9em; cursor:pointer; flex:1; width:auto; margin:0; color:#fff; }
  #net button:disabled { background:#333 !important; color:#777; cursor:not-allowed; }
  #net .sil { background:#cc8822; }
  #net .clr { background:#22aa44; }
  #net .cmd { background:#2d4f7c; }
  #net .cmd.warn { background:#7c3a2d; }
  #net .last { color:#9cf; font-size:0.85em; margin-bottom:8px; min-height:1.1em; }
  #net .label { color:#888; font-size:0.85em; }
  #net pre { background:#000; border-radius:4px; padding:8px; margin:8px 0 0 0; color:#8f8; font-size:0.75em;
        height:150px; overflow-y:auto; white-space:pre-wrap; }
  #netmsg { font-size:0.85em; color:#aaa; min-height:1.2em; margin-bottom:8px; }
)HTML";

static const char NET_PANEL_HTML[] PROGMEM = R"HTML(
<div id="net">
  <div id="netmsg"></div>
  <div id="nodes"></div>
  <div class="card">
    <div class="label">Network events</div>
    <pre id="netlog"></pre>
  </div>
  <div class="card">
    <div class="label">Radio / mesh counters</div>
    <div class="meta" id="netstats">--</div>
  </div>
</div>
)HTML";

static const char NET_PANEL_JS[] PROGMEM = R"HTML(
const CMD_NAME = { 1: 'Test fire', 2: 'Recalibrate', 3: 'Reboot', 4: 'Status' };
const ACK_TEXT = { 0: 'done', 1: 'not supported by that node', 2: 'refused (busy / alarm active)' };
let netStarted = false;
let netSeenNode = {};     // id -> true once seen
let netWasStale = {};     // id -> bool
let netSeenAlarm = {};    // "origin:episode" -> silenced?
let netSeenCmd = {};      // "node:id:cmd" -> last state logged

function netLog(text) {
  const el = document.getElementById('netlog');
  el.textContent += new Date().toLocaleTimeString() + '  ' + text + '\n';
  const lines = el.textContent.split('\n');
  if (lines.length > 80) el.textContent = lines.slice(lines.length - 80).join('\n');
  el.scrollTop = el.scrollHeight;
}

function netAct(kind, node) {
  const what = node ? ('node ' + node) : 'ALL nodes';
  if (kind === 'clear' && !confirm('Clear the alarm on ' + what + ' for the whole network?')) return;
  const msg = document.getElementById('netmsg');
  msg.textContent = '';
  fetch('/' + kind + '?node=' + node, { method: 'POST' }).then(r => r.text()).then(t => {
    msg.textContent = t;
    netLog((kind === 'clear' ? 'CLEAR' : 'SILENCE') + ' sent for ' + what + ' - ' + t);
    if (typeof refresh === 'function') refresh();
  }).catch(() => { msg.textContent = 'Request failed'; });
}

function netCmd(node, cmd) {
  if (cmd === 1 && !confirm('Start a TEST fire alarm on node ' + node + '? Every buzzer in the network will sound.')) return;
  if (cmd === 3 && !confirm('Reboot node ' + node + '? It will be offline for about 30 seconds.')) return;
  if (cmd === 2 && !confirm('Recalibrate the dust baseline on node ' + node + '? Make sure the air near it is smoke-free.')) return;
  const msg = document.getElementById('netmsg');
  msg.textContent = '';
  fetch('/cmd?node=' + node + '&cmd=' + cmd, { method: 'POST' }).then(r => r.text()).then(t => {
    msg.textContent = t;
    netLog(CMD_NAME[cmd] + ' requested for node ' + node + ' - ' + t);
    if (typeof refresh === 'function') refresh();
  }).catch(() => { msg.textContent = 'Request failed'; });
}

function lastCmdLine(id, cmds) {
  const mine = cmds.filter(c => c.node === id).sort((a, b) => a.age - b.age);
  if (!mine.length) return '';
  const c = mine[0];
  const name = CMD_NAME[c.cmd] || ('command ' + c.cmd);
  if (c.state === 0) return name + ': sent, waiting for reply... (' + c.age + 's)';
  if (c.state === 1) return name + ': ' + (ACK_TEXT[c.result] || ('result ' + c.result)) + ' (' + c.age + 's ago)';
  return name + ': no reply - node out of range, off, or the reply was lost (' + c.age + 's ago)';
}

function nodeCard(id, p, alarms, cmds) {
  const inAlarm = alarms.length > 0 || (p && p.alarm);
  const stale = p && p.stale && !inAlarm;
  const allSil = alarms.length > 0 && alarms.every(a => a.silenced);
  const cls = 'card' + (inAlarm ? ' alarm' : (stale ? ' stale' : ''));
  let pill = inAlarm ? '<span class="pill alarm">FIRE</span>' : (stale ? '<span class="pill stale">UNKNOWN</span>' : '<span class="pill ok">OK</span>');
  if (allSil) pill += '<span class="pill sil">buzzer silenced</span>';
  const meta = [];
  if (p) {
    meta.push('heard ' + p.age + 's ago via ' + (p.hops === 0 ? 'direct link' : p.hops + ' hop' + (p.hops > 1 ? 's' : '')));
    meta.push('rssi ' + p.rssi + ' dBm, snr ' + p.snr + ' dB (last hop)');
    if (p.hb) {
      const h = [];
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
  const dis = alarms.length ? '' : 'disabled';
  return '<div class="' + cls + '"><div class="head"><span class="name">Node ' + id + '</span><span>' + pill + '</span></div>'
    + '<div class="meta">' + meta.join('<br>') + '</div>'
    + '<div class="btns"><button class="sil" ' + dis + ' onclick="netAct(\'silence\',' + id + ')">Silence</button>'
    + '<button class="clr" ' + dis + ' onclick="netAct(\'clear\',' + id + ')">Clear alarm</button></div>'
    + '<div class="btns">'
    + '<button class="cmd" onclick="netCmd(' + id + ',4)">Status</button>'
    + '<button class="cmd" onclick="netCmd(' + id + ',2)">Recalibrate</button>'
    + '<button class="cmd warn" onclick="netCmd(' + id + ',1)">Test fire</button>'
    + '<button class="cmd warn" onclick="netCmd(' + id + ',3)">Reboot</button></div>'
    + '<div class="last">' + lastCmdLine(id, cmds) + '</div></div>';
}

// d = parsed /status JSON (needs d.mesh). Draws the node cards, log and counters.
function netRefresh(d) {
  const m = d.mesh;
  const self = m.addr;
  const cmds = m.cmds || [];
  const nodes = {};
  m.peers.forEach(p => { if (p.id !== self) nodes[p.id] = { p: p, alarms: [] }; });
  m.alarms.forEach(a => { if (a.origin !== self) (nodes[a.origin] = nodes[a.origin] || { p: null, alarms: [] }).alarms.push(a); });
  const ids = Object.keys(nodes).map(Number).sort((a, b) => a - b);
  document.getElementById('nodes').innerHTML = ids.length
    ? ids.map(id => nodeCard(id, nodes[id].p, nodes[id].alarms, cmds)).join('')
    : '<div class="card"><div class="meta">No other nodes heard yet. Nodes send a heartbeat about every 30 s.</div></div>';

  ids.forEach(id => {
    const p = nodes[id].p;
    if (!netSeenNode[id]) { netSeenNode[id] = true; netLog('Node ' + id + (netStarted ? ' appeared' : ' heard')); }
    const st = !!(p && p.stale);
    if (netWasStale[id] !== undefined && netWasStale[id] !== st) netLog('Node ' + id + (st ? ' went UNKNOWN (no heartbeat)' : ' is back'));
    netWasStale[id] = st;
  });
  const cur = {};
  m.alarms.forEach(a => {
    const k = a.origin + ':' + a.episode;
    cur[k] = a.silenced;
    if (netSeenAlarm[k] === undefined) netLog('FIRE from node ' + a.origin + ' (episode ' + a.episode + ')');
    else if (!netSeenAlarm[k] && a.silenced) netLog('Alarm on node ' + a.origin + ' silenced');
  });
  Object.keys(netSeenAlarm).forEach(k => { if (cur[k] === undefined) netLog('Alarm on node ' + k.split(':')[0] + ' ended (cleared)'); });
  netSeenAlarm = cur;
  cmds.forEach(c => {
    const k = c.node + ':' + c.id + ':' + c.cmd;
    if (netSeenCmd[k] !== c.state && c.state !== 0) {
      netLog((CMD_NAME[c.cmd] || 'Command') + ' on node ' + c.node + ': '
        + (c.state === 1 ? (ACK_TEXT[c.result] || 'result ' + c.result) : 'NO REPLY'));
    }
    netSeenCmd[k] = c.state;
  });
  netStarted = true;

  const s = m.stats;
  document.getElementById('netstats').textContent = 'node ' + m.addr + '  rx ' + s.rx + '  tx ' + s.tx + '  relayed ' + s.relayed
    + '  dup ' + s.dup + '  replay ' + s.replay + '  authFail ' + s.authFail + '  foreign ' + s.foreign
    + '  crcErr ' + s.crcErr + '  lbt ' + s.lbtDefers + '  cmdRx ' + s.cmdRx + '  ackRx ' + s.ackRx
    + '  | last rssi ' + s.rssi + ' snr ' + s.snr;
}
)HTML";

#endif
