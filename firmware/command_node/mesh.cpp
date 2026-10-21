#include <Arduino.h>
#include <Preferences.h>
#include "mesh.h"
#include "mesh_proto.h"
#include "mesh_radio.h"
#include "config.h"
#include "state.h"
#include "alarm.h"

MeshStats meshStats;

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------
struct MeshPeer {
  bool             used;
  uint8_t          id;
  MeshReplayWindow rp;           // replay + duplicate window for packets ORIGINATED by this node
  bool             hasCleared;   // highest episode of THIS node's alarms that has been cleared
  uint16_t         clearedEp;
  uint32_t         lastHeardMs;  // 0 = never heard directly/relayed from it
  int16_t          rssi;         // of the last hop that delivered its latest packet
  int8_t           snr;
  uint8_t          hops;
  bool             hasHb;
  uint8_t          battPct;
  int16_t          tempC10;
  uint8_t          flags;
};
static MeshPeer peers[MESH_MAX_PEERS];

struct MeshAlarm {
  bool     used;
  uint8_t  origin;
  uint16_t episode;
  bool     silenced;
  uint32_t lastHeardMs;
  int16_t  rssi;
};
static MeshAlarm alarmTbl[MESH_MAX_ALARMS];

struct MeshCtrlJob {              // CLEAR / SILENCE that is sent several times
  bool     used;
  uint8_t  type;
  uint8_t  alarmOrigin;
  uint16_t episode;
  uint8_t  remaining;
  uint32_t nextMs;
};
static MeshCtrlJob ctrlJobs[MESH_MAX_CTRL];

// ---------------------------------------------------------------------------
// Own identity / counters
// ---------------------------------------------------------------------------
static uint16_t ownEpoch = 0;       // boot epoch, persisted in NVS, +1 every boot
static uint16_t ownSeq = 0;         // per-boot sequence, RAM only (no per-packet flash wear)
static uint16_t episodeSeq = 0;     // last local episode number, persisted per trip

static bool     localEpisodeActive = false;
static uint16_t localEpisode = 0;
static bool     localSilenced = false;
static uint32_t nextFireMs = 0;
static uint32_t nextHbMs = 0;
static bool     rearmHolding = false;
static uint32_t rearmUntilMs = 0;

static void saveEpoch() {
  Preferences p;
  if (p.begin("mesh", false)) { p.putUShort("epoch", ownEpoch); p.end(); }
}

static void saveEpisode() {
  Preferences p;
  if (p.begin("mesh", false)) { p.putUShort("episode", episodeSeq); p.end(); }
}

// counter = (epoch << 16) | seq. If seq runs out within one boot, the epoch is
// bumped (and persisted) so counters stay strictly increasing.
static uint32_t nextCounter() {
  if (ownSeq == 0xFFFF) {
    ownEpoch++;
    saveEpoch();
    ownSeq = 0;
  }
  ownSeq++;
  return ((uint32_t)ownEpoch << 16) | ownSeq;
}

// ---------------------------------------------------------------------------
// Table helpers
// ---------------------------------------------------------------------------
static MeshPeer *findPeer(uint8_t id, bool create) {
  MeshPeer *freeSlot = nullptr;
  for (int i = 0; i < MESH_MAX_PEERS; i++) {
    if (peers[i].used && peers[i].id == id) return &peers[i];
    if (!peers[i].used && !freeSlot) freeSlot = &peers[i];
  }
  if (!create || !freeSlot) return nullptr;
  memset(freeSlot, 0, sizeof(MeshPeer));
  freeSlot->used = true;
  freeSlot->id = id;
  return freeSlot;
}

static MeshAlarm *findAlarm(uint8_t origin, uint16_t episode) {
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    if (alarmTbl[i].used && alarmTbl[i].origin == origin && alarmTbl[i].episode == episode) return &alarmTbl[i];
  }
  return nullptr;
}

// A new alarm must always fit: if the table is full, the entry heard longest
// ago is replaced (4 simultaneous episodes is already far beyond a normal fire).
static MeshAlarm *addAlarm(uint8_t origin, uint16_t episode) {
  MeshAlarm *slot = nullptr;
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    if (!alarmTbl[i].used) { slot = &alarmTbl[i]; break; }
  }
  if (!slot) {
    slot = &alarmTbl[0];
    for (int i = 1; i < MESH_MAX_ALARMS; i++) {
      if ((int32_t)(alarmTbl[i].lastHeardMs - slot->lastHeardMs) < 0) slot = &alarmTbl[i];
    }
  }
  memset(slot, 0, sizeof(MeshAlarm));
  slot->used = true;
  slot->origin = origin;
  slot->episode = episode;
  return slot;
}

int meshActiveAlarmCount() {
  int n = 0;
  for (int i = 0; i < MESH_MAX_ALARMS; i++) if (alarmTbl[i].used) n++;
  return n;
}

uint8_t meshFirstAlarmOrigin() {
  for (int i = 0; i < MESH_MAX_ALARMS; i++) if (alarmTbl[i].used) return alarmTbl[i].origin;
  return 0;
}

// Pushes the alarm table's summary into the buzzer/LED layer.
static void applyAlarmState() {
  bool active = false;
  bool allSilenced = true;
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    if (!alarmTbl[i].used) continue;
    active = true;
    if (!alarmTbl[i].silenced) allSilenced = false;
  }
  alarmApply(active, active && allSilenced);
}

static bool isCleared(const MeshPeer *p, uint16_t episode) {
  return p && p->hasCleared && meshEpisodeDiff(episode, p->clearedEp) <= 0;
}

static void markCleared(uint8_t alarmOrigin, uint16_t episode) {
  MeshPeer *p = findPeer(alarmOrigin, true);
  if (!p) return;
  if (!p->hasCleared || meshEpisodeDiff(episode, p->clearedEp) > 0) {
    p->hasCleared = true;
    p->clearedEp = episode;
  }
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------
static void sendPacket(MeshPacket &p, RadioPrio prio) {
  p.origin = NODE_ADDR;
  p.counter = nextCounter();
  uint8_t buf[MESH_MAX_PKT];
  size_t n = meshEncode(p, buf);
  if (radioQueueTx(buf, (uint8_t)n, 0, prio)) meshStats.txOriginated++;
}

static void sendFire() {
  MeshPacket p;
  memset(&p, 0, sizeof(p));
  p.type = MESH_T_FIRE;
  p.ttl = FIRE_TTL;
  p.flags = localSilenced ? MESH_F_SILENCED : 0;
  p.episode = localEpisode;
  int d = baselineReady ? (dustRawLast - dustBaseline) : 0;
  p.dustDelta = (int16_t)constrain(d, -32000, 32000);
  sendPacket(p, RADIO_PRIO_HIGH);
}

static void sendHeartbeat() {
  MeshPacket p;
  memset(&p, 0, sizeof(p));
  p.type = MESH_T_HEARTBEAT;
  p.ttl = HB_TTL;
  p.flags = (fireActive ? MESH_F_ALARM : 0) | (usbPowerDetected ? MESH_F_EXT_POWER : 0);
  p.battPct = (batteryPercent >= 0 && batteryPercent <= 100) ? (uint8_t)batteryPercent : 255;   // 255 = no battery reading (command node)
  p.tempC10 = bmpOk ? (int16_t)lroundf(lastTemp * 10.0f) : MESH_TEMP_NA;
  sendPacket(p, RADIO_PRIO_LOW);
}

static void sendControl(uint8_t type, uint8_t alarmOrigin, uint16_t episode) {
  MeshPacket p;
  memset(&p, 0, sizeof(p));
  p.type = type;
  p.ttl = CTRL_TTL;
  p.alarmOrigin = alarmOrigin;
  p.episode = episode;
  sendPacket(p, RADIO_PRIO_HIGH);
}

// CLEAR / SILENCE go out CTRL_REPEATS times (new counter each time) because a
// single lost control packet would leave part of the network still sounding.
static void queueControl(uint8_t type, uint8_t alarmOrigin, uint16_t episode) {
  for (int i = 0; i < MESH_MAX_CTRL; i++) {
    MeshCtrlJob &j = ctrlJobs[i];
    if (j.used && j.type == type && j.alarmOrigin == alarmOrigin && j.episode == episode) return;  // already pending
  }
  for (int i = 0; i < MESH_MAX_CTRL; i++) {
    MeshCtrlJob &j = ctrlJobs[i];
    if (j.used) continue;
    j.used = true;
    j.type = type;
    j.alarmOrigin = alarmOrigin;
    j.episode = episode;
    j.remaining = CTRL_REPEATS;
    j.nextMs = millis();
    return;
  }
  // Job table full: send once immediately rather than lose the command.
  sendControl(type, alarmOrigin, episode);
}

// Weakest-heard relays first: the stronger the packet was received, the closer
// we are to whoever just sent it, so wait longer (and let farther nodes carry it on).
static uint32_t relayDelayMs(int16_t rssi) {
  long r = constrain((long)rssi, (long)RELAY_RSSI_FAR, (long)RELAY_RSSI_NEAR);
  long base = (r - RELAY_RSSI_FAR) * (long)RELAY_MAX_DELAY_MS / (RELAY_RSSI_NEAR - RELAY_RSSI_FAR);
  return (uint32_t)base + (uint32_t)random(0, RELAY_JITTER_MS + 1);
}

// Re-broadcast the received bytes unchanged except TTL (which the tag excludes).
static void relay(const uint8_t *raw, uint8_t len, uint8_t newTtl, int16_t rssi, RadioPrio prio) {
  uint8_t buf[MESH_MAX_PKT];
  memcpy(buf, raw, len);
  buf[3] = newTtl;
  if (radioQueueTx(buf, len, relayDelayMs(rssi), prio)) meshStats.txRelayed++;
}

// ---------------------------------------------------------------------------
// Local episode lifecycle
// ---------------------------------------------------------------------------
static void endLocalEpisode() {
  localEpisodeActive = false;
  localSilenced = false;
  rearmHolding = true;
  rearmUntilMs = millis() + REARM_HOLDOFF_MS;
  // Let the sensor re-evaluate from scratch; it cannot start a new episode
  // until the hold-off has passed (see meshRearmHeld()).
  smokePresent = false;
  highReadingCount = 0;
  lowReadingCount = 0;
}

void meshStartLocalEpisode() {
  if (localEpisodeActive) return;
  episodeSeq++;
  if (episodeSeq == 0) episodeSeq = 1;
  saveEpisode();
  localEpisode = episodeSeq;
  localEpisodeActive = true;
  localSilenced = false;

  MeshAlarm *a = addAlarm(NODE_ADDR, localEpisode);
  a->lastHeardMs = millis();
  applyAlarmState();

  Serial.print("[MESH] LOCAL FIRE episode ");
  Serial.println(localEpisode);
  sendFire();
  nextFireMs = millis() + FIRE_REANNOUNCE_MS - FIRE_JITTER_MS + random(0, 2 * FIRE_JITTER_MS + 1);
}

int meshClearOrigin(uint8_t origin) {
  int cleared = 0;
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    if (!alarmTbl[i].used) continue;
    if (origin != MESH_ALL_ORIGINS && alarmTbl[i].origin != origin) continue;
    queueControl(MESH_T_CLEAR, alarmTbl[i].origin, alarmTbl[i].episode);
    markCleared(alarmTbl[i].origin, alarmTbl[i].episode);
    alarmTbl[i].used = false;
    cleared++;
  }
  if (localEpisodeActive && (origin == MESH_ALL_ORIGINS || origin == NODE_ADDR)) endLocalEpisode();
  applyAlarmState();
  Serial.print("[MESH] CLEAR sent for ");
  Serial.print(cleared);
  Serial.println(" alarm(s)");
  return cleared;
}

int meshSilenceOrigin(uint8_t origin) {
  int n = 0;
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    if (!alarmTbl[i].used) continue;
    if (origin != MESH_ALL_ORIGINS && alarmTbl[i].origin != origin) continue;
    alarmTbl[i].silenced = true;
    queueControl(MESH_T_SILENCE, alarmTbl[i].origin, alarmTbl[i].episode);
    n++;
  }
  if (localEpisodeActive && (origin == MESH_ALL_ORIGINS || origin == NODE_ADDR)) localSilenced = true;
  applyAlarmState();
  Serial.print("[MESH] SILENCE sent for ");
  Serial.print(n);
  Serial.println(" alarm(s)");
  return n;
}

void meshLocalClearAll() { meshClearOrigin(MESH_ALL_ORIGINS); }
void meshLocalSilence()  { meshSilenceOrigin(MESH_ALL_ORIGINS); }

bool meshLocalEpisodeActive() { return localEpisodeActive; }

bool meshRearmHeld() {
  if (!rearmHolding) return false;
  if ((int32_t)(millis() - rearmUntilMs) >= 0) { rearmHolding = false; return false; }
  return true;
}

// ---------------------------------------------------------------------------
// Receive path
// ---------------------------------------------------------------------------
static uint8_t typeTtl(uint8_t type) {
  return type == MESH_T_FIRE ? FIRE_TTL : (type == MESH_T_HEARTBEAT ? HB_TTL : CTRL_TTL);
}

static void handleFire(const MeshPacket &p, MeshPeer *pe, const uint8_t *raw, uint8_t len, int16_t rssi) {
  meshStats.rxFire++;

  // Cleared check comes before anything else: a cleared episode is neither
  // sounded nor relayed, so stale re-announcements die out.
  if (isCleared(pe, p.episode)) { meshStats.rxClearedDrop++; return; }

  MeshAlarm *a = findAlarm(p.alarmOrigin, p.episode);
  if (!a) {
    a = addAlarm(p.alarmOrigin, p.episode);
    Serial.print("[MESH] FIRE from node ");
    Serial.print(p.alarmOrigin);
    Serial.print(" episode ");
    Serial.print(p.episode);
    Serial.print(" (rssi ");
    Serial.print(rssi);
    Serial.println(")");
  }
  a->lastHeardMs = millis();
  a->rssi = rssi;
  if (p.flags & MESH_F_SILENCED) a->silenced = true;   // silence is sticky per episode
  applyAlarmState();

  if (p.ttl > 1) relay(raw, len, p.ttl - 1, rssi, RADIO_PRIO_HIGH);
}

static void handleHeartbeat(const MeshPacket &p, MeshPeer *pe, const uint8_t *raw, uint8_t len, int16_t rssi) {
  meshStats.rxHeartbeat++;
  pe->hasHb = true;
  pe->battPct = p.battPct;
  pe->tempC10 = p.tempC10;
  pe->flags = p.flags;
  if (p.ttl > 1) relay(raw, len, p.ttl - 1, rssi, RADIO_PRIO_LOW);
}

static void handleClear(const MeshPacket &p, const uint8_t *raw, uint8_t len, int16_t rssi) {
  meshStats.rxClear++;
  markCleared(p.alarmOrigin, p.episode);

  int removed = 0;
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    MeshAlarm &a = alarmTbl[i];
    if (a.used && a.origin == p.alarmOrigin && meshEpisodeDiff(a.episode, p.episode) <= 0) {
      a.used = false;
      removed++;
    }
  }
  // Our own episode was cleared remotely: stop re-announcing it.
  if (p.alarmOrigin == NODE_ADDR && localEpisodeActive && meshEpisodeDiff(localEpisode, p.episode) <= 0) {
    endLocalEpisode();
  }
  if (removed || p.alarmOrigin == NODE_ADDR) {
    Serial.print("[MESH] CLEAR from node ");
    Serial.print(p.origin);
    Serial.print(" for node ");
    Serial.print(p.alarmOrigin);
    Serial.print(" episode ");
    Serial.println(p.episode);
  }
  applyAlarmState();
  if (p.ttl > 1) relay(raw, len, p.ttl - 1, rssi, RADIO_PRIO_HIGH);
}

static void handleSilence(const MeshPacket &p, const uint8_t *raw, uint8_t len, int16_t rssi) {
  meshStats.rxSilence++;
  MeshAlarm *a = findAlarm(p.alarmOrigin, p.episode);
  if (a && !a->silenced) {
    a->silenced = true;
    Serial.print("[MESH] SILENCE from node ");
    Serial.print(p.origin);
    Serial.print(" for node ");
    Serial.print(p.alarmOrigin);
    Serial.print(" episode ");
    Serial.println(p.episode);
  }
  if (p.alarmOrigin == NODE_ADDR && localEpisodeActive && localEpisode == p.episode) localSilenced = true;
  applyAlarmState();
  if (p.ttl > 1) relay(raw, len, p.ttl - 1, rssi, RADIO_PRIO_HIGH);
}

static void onRadioRx(const uint8_t *raw, uint8_t len, int16_t rssi, int8_t snr) {
  meshStats.rxTotal++;
  meshStats.lastRssi = rssi;
  meshStats.lastSnr = snr;

  MeshPacket p;
  switch (meshDecode(raw, len, p)) {          // magic -> version/type/length -> HMAC
    case MESH_OK: break;
    case MESH_ERR_MAGIC: meshStats.rxBadMagic++; return;
    case MESH_ERR_AUTH:
      meshStats.rxAuthFail++;
      Serial.println("[MESH] dropped packet: HMAC failed");
      return;
    default: meshStats.rxMalformed++; return;
  }

  if (p.origin == NODE_ADDR) { meshStats.rxOwnEcho++; return; }   // our own flood coming back

  MeshPeer *pe = findPeer(p.origin, true);
  if (!pe) { meshStats.rxPeerFull++; return; }

  MeshReplayResult rr = meshReplayCheck(pe->rp, p.counter);        // authentic, now replay/duplicate
  if (rr == MESH_RP_DUP) { meshStats.rxDup++; return; }
  if (rr == MESH_RP_OLD) { meshStats.rxReplay++; return; }

  pe->lastHeardMs = millis();
  pe->rssi = rssi;
  pe->snr = snr;
  uint8_t full = typeTtl(p.type);
  pe->hops = (p.ttl <= full) ? (full - p.ttl) : 0;

  switch (p.type) {
    case MESH_T_FIRE:      handleFire(p, pe, raw, len, rssi); break;
    case MESH_T_HEARTBEAT: handleHeartbeat(p, pe, raw, len, rssi); break;
    case MESH_T_CLEAR:     handleClear(p, raw, len, rssi); break;
    case MESH_T_SILENCE:   handleSilence(p, raw, len, rssi); break;
  }
}

// ---------------------------------------------------------------------------
// Init / periodic work
// ---------------------------------------------------------------------------
void meshInit() {
  memset(peers, 0, sizeof(peers));
  memset(alarmTbl, 0, sizeof(alarmTbl));
  memset(ctrlJobs, 0, sizeof(ctrlJobs));
  memset(&meshStats, 0, sizeof(meshStats));

  Preferences p;
  if (p.begin("mesh", false)) {
    ownEpoch = p.getUShort("epoch", 0);
    episodeSeq = p.getUShort("episode", 0);
    ownEpoch++;
    if (ownEpoch == 0) ownEpoch = 1;
    p.putUShort("epoch", ownEpoch);
    p.end();
  } else {
    // NVS unavailable: fall back to a random epoch so counters still (very
    // probably) exceed what peers last saw. Replay protection is weaker here.
    ownEpoch = (uint16_t)(esp_random() | 1);
    Serial.println("[MESH] WARNING: NVS unavailable, using a random boot epoch");
  }
  ownSeq = 0;

  meshLoadKey();
  radioSetRxHandler(onRadioRx);
  radioInit();

  nextHbMs = millis() + 5000 + random(0, 5000);   // first heartbeat soon after boot

  Serial.print("[MESH] node ");
  Serial.print(NODE_ADDR);
  Serial.print(" epoch ");
  Serial.print(ownEpoch);
  Serial.print(" last episode ");
  Serial.println(episodeSeq);
}

void meshService() {
  radioService();

  uint32_t now = millis();

  // FIRE is a state, not an event: the origin keeps announcing it.
  if (localEpisodeActive && (int32_t)(now - nextFireMs) >= 0) {
    sendFire();
    nextFireMs = now + FIRE_REANNOUNCE_MS - FIRE_JITTER_MS + random(0, 2 * FIRE_JITTER_MS + 1);
  }

  // Repeated CLEAR / SILENCE.
  for (int i = 0; i < MESH_MAX_CTRL; i++) {
    MeshCtrlJob &j = ctrlJobs[i];
    if (!j.used || (int32_t)(now - j.nextMs) < 0) continue;
    sendControl(j.type, j.alarmOrigin, j.episode);
    if (--j.remaining == 0) j.used = false;
    else j.nextMs = now + CTRL_REPEAT_MS - CTRL_JITTER_MS + random(0, 2 * CTRL_JITTER_MS + 1);
  }

  // Heartbeats are suppressed while any alarm is active (airtime belongs to FIRE).
  if ((int32_t)(now - nextHbMs) >= 0) {
    nextHbMs = now + HB_INTERVAL_MS - 3000 + random(0, 6001);
    if (!fireActive) sendHeartbeat();
  }
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
int meshPeerCount() {
  int n = 0;
  uint32_t now = millis();
  for (int i = 0; i < MESH_MAX_PEERS; i++) {
    if (peers[i].used && peers[i].lastHeardMs != 0 && (now - peers[i].lastHeardMs) < PEER_STALE_MS) n++;
  }
  return n;
}

String meshStatusJson() {
  uint32_t now = millis();
  String j = "\"mesh\":{";
  j += "\"addr\":" + String(NODE_ADDR) + ",";
  j += "\"epoch\":" + String(ownEpoch) + ",";
  j += "\"localEpisode\":" + String(localEpisodeActive ? (int)localEpisode : -1) + ",";

  j += "\"alarms\":[";
  bool first = true;
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    if (!alarmTbl[i].used) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"origin\":" + String(alarmTbl[i].origin) + ",\"episode\":" + String(alarmTbl[i].episode)
       + ",\"silenced\":" + String(alarmTbl[i].silenced ? "true" : "false") + "}";
  }
  j += "],\"peers\":[";
  first = true;
  for (int i = 0; i < MESH_MAX_PEERS; i++) {
    const MeshPeer &p = peers[i];
    if (!p.used || p.lastHeardMs == 0) continue;
    if (!first) j += ",";
    first = false;
    uint32_t age = (now - p.lastHeardMs) / 1000;
    bool stale = (now - p.lastHeardMs) >= PEER_STALE_MS;
    j += "{\"id\":" + String(p.id) + ",\"age\":" + String(age) + ",\"stale\":" + String(stale ? "true" : "false")
       + ",\"rssi\":" + String(p.rssi) + ",\"snr\":" + String(p.snr) + ",\"hops\":" + String(p.hops)
       + ",\"hb\":" + String(p.hasHb ? "true" : "false") + ",\"batt\":" + String(p.battPct)
       + ",\"ext\":" + String((p.flags & MESH_F_EXT_POWER) ? "true" : "false")
       + ",\"hasTemp\":" + String(p.tempC10 != MESH_TEMP_NA ? "true" : "false")
       + ",\"temp\":" + String(p.tempC10 / 10.0f, 1)
       + ",\"alarm\":" + String((p.flags & MESH_F_ALARM) ? "true" : "false") + "}";
  }
  j += "],\"stats\":{";
  j += "\"rx\":" + String(meshStats.rxTotal) + ",\"tx\":" + String(meshStats.txOriginated)
     + ",\"relayed\":" + String(meshStats.txRelayed) + ",\"dup\":" + String(meshStats.rxDup)
     + ",\"replay\":" + String(meshStats.rxReplay) + ",\"authFail\":" + String(meshStats.rxAuthFail)
     + ",\"clearedDrop\":" + String(meshStats.rxClearedDrop) + ",\"foreign\":" + String(meshStats.rxBadMagic)
     + ",\"crcErr\":" + String(radioStats.rxCrcError) + ",\"txTimeout\":" + String(radioStats.txTimeout)
     + ",\"txDropped\":" + String(radioStats.txDropped) + ",\"lbtDefers\":" + String(radioStats.txDeferred)
     + ",\"rssi\":" + String(meshStats.lastRssi) + ",\"snr\":" + String(meshStats.lastSnr);
  j += "}}";
  return j;
}

void meshPrintStatus() {
  uint32_t now = millis();
  Serial.print("[MESH] node "); Serial.print(NODE_ADDR);
  Serial.print(" epoch "); Serial.print(ownEpoch);
  Serial.print(" alarms "); Serial.println(meshActiveAlarmCount());
  for (int i = 0; i < MESH_MAX_ALARMS; i++) {
    if (!alarmTbl[i].used) continue;
    Serial.print("  alarm origin="); Serial.print(alarmTbl[i].origin);
    Serial.print(" episode="); Serial.print(alarmTbl[i].episode);
    Serial.println(alarmTbl[i].silenced ? " (silenced)" : "");
  }
  for (int i = 0; i < MESH_MAX_PEERS; i++) {
    const MeshPeer &p = peers[i];
    if (!p.used || p.lastHeardMs == 0) continue;
    Serial.print("  peer "); Serial.print(p.id);
    Serial.print(" heard "); Serial.print((now - p.lastHeardMs) / 1000); Serial.print("s ago");
    Serial.print(" rssi "); Serial.print(p.rssi);
    Serial.print(" snr "); Serial.print(p.snr);
    Serial.print(" hops "); Serial.print(p.hops);
    if (p.hasHb) { Serial.print(" batt "); Serial.print(p.battPct); Serial.print("%"); }
    Serial.println();
  }
  Serial.print("  rx="); Serial.print(meshStats.rxTotal);
  Serial.print(" tx="); Serial.print(meshStats.txOriginated);
  Serial.print(" relayed="); Serial.print(meshStats.txRelayed);
  Serial.print(" dup="); Serial.print(meshStats.rxDup);
  Serial.print(" replay="); Serial.print(meshStats.rxReplay);
  Serial.print(" authFail="); Serial.print(meshStats.rxAuthFail);
  Serial.print(" foreign="); Serial.print(meshStats.rxBadMagic);
  Serial.print(" crcErr="); Serial.print(radioStats.rxCrcError);
  Serial.print(" lbtDefers="); Serial.println(radioStats.txDeferred);
}
