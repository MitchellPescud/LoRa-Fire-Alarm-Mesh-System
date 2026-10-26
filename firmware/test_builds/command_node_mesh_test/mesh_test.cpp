#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "mesh_test.h"
#include "mesh_proto.h"
#include "mesh_radio.h"
#include "mesh.h"
#include "config.h"

// ---------------------------------------------------------------------------
// Test layer. See mesh_test.h for the overview.
//
// Serial commands (115200, newline; case-insensitive):
//   test ping <node> [count=20] [gap_s=3]   STATUS command to <node>, one at a time.
//                                           Measures round-trip time and how many CMD
//                                           copies were needed (1 = got through first time).
//   test alarm [count=5] [hold_s=20] [silence=1]
//                                           This node raises a FIRE episode, optionally
//                                           silences it halfway, clears it after hold_s,
//                                           waits 10 s, repeats. Receivers log when each
//                                           step reached them.
//   test hb [count=50] [gap_ms=2000]        This node sends <count> heartbeats <gap_ms> apart
//                                           (delivery / relay load test).
//   test stop | test status
//   block [id ...] | block none             ignore frames sent directly by these node ids
//   unblock <id ...>                        (saved in flash; no argument lists them)
//   quiet on|off   pause this node's own periodic heartbeats
//   mute on|off    buzzer (default off = muted during tests)
//   lbt on|off     listen-before-talk (default on)
//   trace on|off   per-packet TX / RX / EV lines (default on)
//   cfg | stats    print the CFG / STAT line now
// ---------------------------------------------------------------------------

bool testTrace = true;
bool testQuiet = false;
bool testMute = true;
bool testLbtEnabled = true;

#define TEST_MAX_BLOCK 4
static uint8_t  blocked[TEST_MAX_BLOCK];     // node ids, 0 = unused
static uint32_t traceDropped = 0;

static inline uint8_t laneOf(uint8_t id) { return id & 0x1F; }

// ---------------------------------------------------------------------------
// Output. Trace lines are dropped (and counted) rather than blocking the radio
// loop if the USB serial buffer is full, e.g. nothing is reading the port.
// ---------------------------------------------------------------------------
static void emitTrace(const char *buf) {
  if (Serial.availableForWrite() < (int)strlen(buf) + 2) { traceDropped++; return; }
  Serial.println(buf);
}
static void emitAlways(const char *buf) { Serial.println(buf); }

void testEvent(const char *fmt, ...) {
  if (!testTrace) return;
  char buf[160];
  int n = snprintf(buf, sizeof(buf), "EV,%lu,", (unsigned long)millis());
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);
  emitTrace(buf);
}

static const char *typeName(uint8_t t) {
  switch (t) {
    case MESH_T_FIRE:      return "FIRE";
    case MESH_T_HEARTBEAT: return "HB";
    case MESH_T_CLEAR:     return "CLEAR";
    case MESH_T_SILENCE:   return "SILENCE";
    case MESH_T_CMD:       return "CMD";
    case MESH_T_ACK:       return "ACK";
    default:               return "?";
  }
}

static uint32_t rd32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// Payload summary: FIRE episode | HB "-" | CLEAR,SILENCE alarmOrigin:episode |
// CMD target:cmd:reqId | ACK target:cmd:reqId:result
static void describe(const uint8_t *pkt, uint8_t len, char *out, size_t n) {
  uint8_t type = pkt[2] & 0x0F;
  const uint8_t *pl = pkt + MESH_HDR_LEN;
  if (len < MESH_HDR_LEN + MESH_TAG_LEN) { snprintf(out, n, "-"); return; }
  switch (type) {
    case MESH_T_FIRE:    snprintf(out, n, "%u", (unsigned)(pl[0] | (pl[1] << 8))); break;
    case MESH_T_CLEAR:
    case MESH_T_SILENCE: snprintf(out, n, "%u:%u", (unsigned)pl[0], (unsigned)(pl[1] | (pl[2] << 8))); break;
    case MESH_T_CMD:     snprintf(out, n, "%u:%u:%u", (unsigned)pl[0], (unsigned)pl[1], (unsigned)pl[2]); break;
    case MESH_T_ACK:     snprintf(out, n, "%u:%u:%u:%u", (unsigned)pl[0], (unsigned)pl[1], (unsigned)pl[2], (unsigned)pl[3]); break;
    default:             snprintf(out, n, "-"); break;
  }
}

// TX,<ms>,<type>,<origin>,<counter>,<ttl>,<lane>,<relay>,<queue_ms>,<lbt_defers>,<len>,<info>
void testTraceTx(const uint8_t *pkt, uint8_t len, uint32_t queueMs, uint8_t defers, uint32_t atMs) {
  if (!testTrace || len < MESH_HDR_LEN) return;
  char info[24];
  describe(pkt, len, info, sizeof(info));
  char buf[128];
  snprintf(buf, sizeof(buf), "TX,%lu,%s,%u,%lu,%u,%u,%u,%lu,%u,%u,%s",
           (unsigned long)atMs, typeName(pkt[2] & 0x0F), (unsigned)pkt[4], (unsigned long)rd32(pkt + 6),
           (unsigned)(pkt[3] & 0x07), (unsigned)(pkt[3] >> 3), (unsigned)(pkt[4] != NODE_ADDR),
           (unsigned long)queueMs, (unsigned)defers, (unsigned)len, info);
  emitTrace(buf);
}

// RX,<ms>,<type>,<origin>,<counter>,<ttl>,<lane>,<hops>,<rssi>,<snr>,<disp>,<info>
// disp: NEW first time heard | DUP flood duplicate | OLD replay window | OWN our own packet relayed back
void testTraceRx(const uint8_t *raw, uint8_t len, int16_t rssi, int8_t snr, uint8_t hops, const char *disp) {
  if (!testTrace || len < MESH_HDR_LEN) return;
  char info[24];
  describe(raw, len, info, sizeof(info));
  char buf[128];
  snprintf(buf, sizeof(buf), "RX,%lu,%s,%u,%lu,%u,%u,%u,%d,%d,%s,%s",
           (unsigned long)millis(), typeName(raw[2] & 0x0F), (unsigned)raw[4], (unsigned long)rd32(raw + 6),
           (unsigned)(raw[3] & 0x07), (unsigned)(raw[3] >> 3), (unsigned)hops, (int)rssi, (int)snr, disp, info);
  emitTrace(buf);
}

void testStampLane(uint8_t *pkt) { pkt[3] = (uint8_t)((pkt[3] & 0x07) | (laneOf(NODE_ADDR) << 3)); }

bool testLaneBlocked(uint8_t lane) {
  for (int i = 0; i < TEST_MAX_BLOCK; i++) if (blocked[i] && laneOf(blocked[i]) == lane) return true;
  return false;
}

// ---------------------------------------------------------------------------
// Config / stats lines
// ---------------------------------------------------------------------------
static float airtimeMs(int payloadBytes) {            // standard LoRa time-on-air, explicit header, CRC on
  static const float bwHz[3] = {125000.0f, 250000.0f, 500000.0f};
  float bw = bwHz[LORA_BW_INDEX < 3 ? LORA_BW_INDEX : 0];
  int sf = LORA_SF;
  float tsym = (float)(1 << sf) / bw * 1000.0f;
  int de = (tsym > 16.0f) ? 1 : 0;
  float num = 8.0f * payloadBytes - 4.0f * sf + 28.0f + 16.0f;
  int nsym = 8 + (int)fmaxf(ceilf(num / (4.0f * (sf - 2 * de))) * (LORA_CR_INDEX + 4), 0.0f);
  return ((float)LORA_PREAMBLE_LEN + 4.25f) * tsym + nsym * tsym;
}

static void blockedList(char *out, size_t n) {
  out[0] = 0;
  bool any = false;
  for (int i = 0; i < TEST_MAX_BLOCK; i++) {
    if (!blocked[i]) continue;
    size_t l = strlen(out);
    snprintf(out + l, n - l, "%s%u", any ? "|" : "", (unsigned)blocked[i]);
    any = true;
  }
  if (!any) snprintf(out, n, "none");
}

static void printCfg() {
  char bl[24];
  blockedList(bl, sizeof(bl));
  char buf[480];
  snprintf(buf, sizeof(buf),
           "CFG,%lu,node=%u,lane=%u,role=%s,fw=meshtest1,sf=%d,bw_khz=%d,pwr_dbm=%d,freq_mhz=%.1f,"
           "airtime18_ms=%.0f,ttl_fire=%u,ttl_ctrl=%u,ttl_hb=%u,fire_ms=%lu,hb_ms=%lu,ctrl_repeats=%u,"
           "ctrl_repeat_ms=%lu,relay_max_ms=%u,relay_jit_ms=%u,blocked=%s,lbt=%s,mute=%s,quiet=%s,trace=%s",
           (unsigned long)millis(), (unsigned)NODE_ADDR, (unsigned)laneOf(NODE_ADDR),
           NODE_ADDR >= MESH_ADDR_COMMAND ? "command" : "fire", (int)LORA_SF,
           LORA_BW_INDEX == 0 ? 125 : (LORA_BW_INDEX == 1 ? 250 : 500), (int)LORA_TX_POWER_DBM,
           LORA_FREQ_HZ / 1000000.0, airtimeMs(18), (unsigned)FIRE_TTL, (unsigned)CTRL_TTL, (unsigned)HB_TTL,
           (unsigned long)FIRE_REANNOUNCE_MS, (unsigned long)HB_INTERVAL_MS, (unsigned)CTRL_REPEATS,
           (unsigned long)CTRL_REPEAT_MS, (unsigned)RELAY_MAX_DELAY_MS, (unsigned)RELAY_JITTER_MS, bl,
           testLbtEnabled ? "on" : "off", testMute ? "on" : "off", testQuiet ? "on" : "off", testTrace ? "on" : "off");
  emitAlways(buf);
}

static void printStats() {
  char buf[400];
  snprintf(buf, sizeof(buf),
           "STAT,%lu,node=%u,rx_total=%lu,rx_blocked=%lu,rx_dup=%lu,rx_replay=%lu,rx_auth_fail=%lu,rx_foreign=%lu,"
           "rx_own_echo=%lu,rx_crc_err=%lu,tx_orig=%lu,tx_relayed=%lu,tx_done=%lu,tx_timeout=%lu,tx_dropped=%lu,"
           "tx_deferred=%lu,cmd_rejected=%lu,trace_dropped=%lu",
           (unsigned long)millis(), (unsigned)NODE_ADDR, (unsigned long)meshStats.rxTotal,
           (unsigned long)meshStats.rxBlocked, (unsigned long)meshStats.rxDup, (unsigned long)meshStats.rxReplay,
           (unsigned long)meshStats.rxAuthFail, (unsigned long)meshStats.rxBadMagic, (unsigned long)meshStats.rxOwnEcho,
           (unsigned long)radioStats.rxCrcError, (unsigned long)meshStats.txOriginated,
           (unsigned long)meshStats.txRelayed, (unsigned long)radioStats.txDone, (unsigned long)radioStats.txTimeout,
           (unsigned long)radioStats.txDropped, (unsigned long)radioStats.txDeferred,
           (unsigned long)meshStats.rxCmdRejected, (unsigned long)traceDropped);
  emitAlways(buf);
}

// ---------------------------------------------------------------------------
// Saved topology (node ids to ignore), kept in flash so a reboot keeps it.
// ---------------------------------------------------------------------------
static void saveBlocked() {
  Preferences p;
  if (!p.begin("mtest", false)) return;
  for (int i = 0; i < TEST_MAX_BLOCK; i++) {
    char k[4] = {'b', (char)('0' + i), 0, 0};
    p.putUShort(k, blocked[i]);
  }
  p.end();
}

static void loadBlocked() {
  memset(blocked, 0, sizeof(blocked));
  Preferences p;
  if (!p.begin("mtest", false)) return;
  for (int i = 0; i < TEST_MAX_BLOCK; i++) {
    char k[4] = {'b', (char)('0' + i), 0, 0};
    uint16_t v = p.getUShort(k, 0);
    blocked[i] = (v > 0 && v < 255) ? (uint8_t)v : 0;
  }
  p.end();
}

static bool addBlocked(int id) {
  if (id < 1 || id > 254 || id == NODE_ADDR) { Serial.println("[TEST] bad node id"); return false; }
  for (int i = 0; i < TEST_MAX_BLOCK; i++) if (blocked[i] == id) return true;
  if (laneOf((uint8_t)id) == laneOf(NODE_ADDR)) {
    Serial.println("[TEST] WARNING: that id shares its sender lane (id & 31) with this node - use different ids");
  }
  for (int i = 0; i < TEST_MAX_BLOCK; i++) {
    if (!blocked[i]) { blocked[i] = (uint8_t)id; return true; }
  }
  Serial.println("[TEST] block list full");
  return false;
}

// ---------------------------------------------------------------------------
// Test runner (one test at a time, driven from testService() in loop())
// ---------------------------------------------------------------------------
enum { T_NONE = 0, T_PING, T_ALARM, T_HB };
#define ALARM_SETTLE_MS  10000UL    // after CLEAR, let the repeated CLEARs and relays finish
#define PING_WARMUP_MS    2500UL    // one heartbeat first so the target has a replay window for us
#define HB_TAIL_MS        3000UL

static struct {
  uint8_t  kind;
  uint8_t  target;
  int      total, launched, finished;
  uint32_t gapMs, holdMs;
  bool     doSilence;
  uint8_t  phase;
  uint32_t nextMs, phaseMs, startMs;
  int      ok, noreply, copiesSum;
  uint32_t rttSum, rttMin, rttMax;
  bool     savedQuiet;
} run;

static void testStart(uint8_t kind) {
  run.kind = kind;
  run.launched = run.finished = 0;
  run.ok = run.noreply = run.copiesSum = 0;
  run.rttSum = run.rttMax = 0;
  run.rttMin = 0xFFFFFFFFUL;
  run.phase = 0;
  run.startMs = millis();
  run.savedQuiet = testQuiet;
  testQuiet = true;                    // this node's background heartbeats would pollute the run
  printCfg();
}

static void testFinish(const char *why) {
  char buf[200];
  uint32_t now = millis();
  switch (run.kind) {
    case T_PING:
      snprintf(buf, sizeof(buf), "RES,%lu,ping,node=%u,sent=%d,ok=%d,noreply=%d,copies_sum=%d,rtt_min_ms=%lu,rtt_mean_ms=%lu,rtt_max_ms=%lu,end=%s",
               (unsigned long)now, (unsigned)run.target, run.launched, run.ok, run.noreply, run.copiesSum,
               (unsigned long)(run.ok ? run.rttMin : 0), (unsigned long)(run.ok ? run.rttSum / run.ok : 0),
               (unsigned long)run.rttMax, why);
      break;
    case T_ALARM:
      snprintf(buf, sizeof(buf), "RES,%lu,alarm,cycles=%d,hold_s=%lu,silence=%d,end=%s",
               (unsigned long)now, run.finished, (unsigned long)(run.holdMs / 1000), run.doSilence ? 1 : 0, why);
      break;
    default:
      snprintf(buf, sizeof(buf), "RES,%lu,hb,sent=%d,gap_ms=%lu,end=%s", (unsigned long)now, run.launched,
               (unsigned long)run.gapMs, why);
      break;
  }
  emitAlways(buf);
  testEvent("TEST_END,%lu", (unsigned long)(now - run.startMs));
  printStats();
  testQuiet = run.savedQuiet;
  run.kind = T_NONE;
}

void testService() {
  if (run.kind == T_NONE) return;
  uint32_t now = millis();

  switch (run.kind) {
    case T_PING:
      if (run.phase == 0) {                          // warm-up heartbeat
        meshSendHeartbeatNow();
        run.nextMs = now + PING_WARMUP_MS;
        run.phase = 1;
      } else if (run.phase == 1) {
        if ((int32_t)(now - run.nextMs) < 0) break;
        if (run.launched >= run.total) { testFinish("done"); break; }
        int r = meshSendCommand(run.target, MESH_CMD_STATUS);
        if (r == 0) { run.launched++; run.phase = 2; }
        else if (r == 1) run.nextMs = now + 500;      // an earlier request is still pending
        else { testFinish("cannot-send"); }
      } else if (run.phase == 2) {
        uint8_t res = 0;
        int st = meshCommandState(run.target, MESH_CMD_STATUS, &res);
        if (st == MESH_CMDSTATE_PENDING || st == MESH_CMDSTATE_NONE) break;
        if (st == MESH_CMDSTATE_DONE) {
          uint32_t rtt = 0; uint8_t copies = 0;
          meshCommandInfo(run.target, MESH_CMD_STATUS, &rtt, &copies);
          run.ok++;
          run.copiesSum += copies;
          run.rttSum += rtt;
          if (rtt < run.rttMin) run.rttMin = rtt;
          if (rtt > run.rttMax) run.rttMax = rtt;
        } else {
          run.noreply++;
        }
        run.finished++;
        run.nextMs = now + run.gapMs;
        run.phase = 1;
      }
      break;

    case T_ALARM:
      if (run.phase == 0) {
        if (meshLocalEpisodeActive()) { testFinish("local-alarm-already-active"); break; }
        run.launched++;
        testEvent("ALARM_CYCLE,%d,%d", run.launched, run.total);
        meshStartLocalEpisode();
        run.phaseMs = now;
        run.phase = run.doSilence ? 1 : 2;
      } else if (run.phase == 1) {
        if (now - run.phaseMs >= run.holdMs / 2) { meshSilenceOrigin(NODE_ADDR); run.phase = 2; }
      } else if (run.phase == 2) {
        if (now - run.phaseMs >= run.holdMs) { meshClearOrigin(NODE_ADDR); run.phaseMs = now; run.phase = 3; }
      } else if (run.phase == 3) {
        if (now - run.phaseMs >= ALARM_SETTLE_MS) {
          run.finished++;
          if (run.finished >= run.total) testFinish("done"); else run.phase = 0;
        }
      }
      break;

    case T_HB:
      if (run.launched < run.total) {
        if ((int32_t)(now - run.nextMs) >= 0) {
          meshSendHeartbeatNow();
          run.launched++;
          run.nextMs = now + run.gapMs;
          run.phaseMs = now;
        }
      } else if (now - run.phaseMs >= HB_TAIL_MS + 700) {
        testFinish("done");
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// Serial commands
// ---------------------------------------------------------------------------
static int tokenize(const char *line, char *store, size_t storeLen, char **tok, int maxTok) {
  strncpy(store, line, storeLen - 1);
  store[storeLen - 1] = 0;
  int n = 0;
  char *p = store;
  while (*p && n < maxTok) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    tok[n++] = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
  }
  return n;
}

static long numArg(char **tok, int n, int idx, long def) { return (idx < n) ? atol(tok[idx]) : def; }

static bool onOff(char **tok, int n, bool *flag, const char *name) {
  if (n >= 2) {
    if (!strcmp(tok[1], "on")) *flag = true;
    else if (!strcmp(tok[1], "off")) *flag = false;
  }
  Serial.print("[TEST] ");
  Serial.print(name);
  Serial.println(*flag ? " on" : " off");
  return true;
}

static void testHelp() {
  Serial.println("[TEST] test ping <node> [count=20] [gap_s=3] | test alarm [count=5] [hold_s=20] [silence=1] | test hb [count=50] [gap_ms=2000] | test stop | test status");
  Serial.println("[TEST] block [id..] | block none | unblock <id..> | quiet|mute|lbt|trace on|off | cfg | stats");
}

bool testHandleSerial(const char *line) {
  char store[96];
  char *tok[8];
  int n = tokenize(line, store, sizeof(store), tok, 8);
  if (n == 0) return false;
  const char *c = tok[0];

  if (!strcmp(c, "test")) {
    const char *sub = (n >= 2) ? tok[1] : "";
    if (!strcmp(sub, "stop")) {
      if (run.kind != T_NONE) testFinish("stopped"); else Serial.println("[TEST] nothing running");
    } else if (!strcmp(sub, "status")) {
      Serial.print("[TEST] ");
      if (run.kind == T_NONE) Serial.println("idle");
      else { Serial.print("running kind="); Serial.print(run.kind); Serial.print(" launched="); Serial.print(run.launched); Serial.print("/"); Serial.println(run.total); }
    } else if (run.kind != T_NONE) {
      Serial.println("[TEST] a test is already running (test stop)");
    } else if (!strcmp(sub, "ping")) {
      long node = numArg(tok, n, 2, 0);
      long count = numArg(tok, n, 3, 20);
      long gap = numArg(tok, n, 4, 3);
      if (node < 1 || node > 254 || node == NODE_ADDR || count < 1 || count > 1000 || gap < 0 || gap > 600) {
        Serial.println("[TEST] usage: test ping <node> [count=20] [gap_s=3]");
        return true;
      }
      run.target = (uint8_t)node; run.total = (int)count; run.gapMs = (uint32_t)gap * 1000UL;
      testStart(T_PING);
      testEvent("TEST_START,ping,%ld,%ld,%ld", node, count, gap);
    } else if (!strcmp(sub, "alarm")) {
      long count = numArg(tok, n, 2, 5);
      long hold = numArg(tok, n, 3, 20);
      long sil = numArg(tok, n, 4, 1);
      if (count < 1 || count > 200 || hold < 4 || hold > 600) {
        Serial.println("[TEST] usage: test alarm [count=5] [hold_s=20] [silence=1]");
        return true;
      }
      run.total = (int)count; run.holdMs = (uint32_t)hold * 1000UL; run.doSilence = (sil != 0);
      testStart(T_ALARM);
      testEvent("TEST_START,alarm,%ld,%ld,%ld", count, hold, sil);
    } else if (!strcmp(sub, "hb")) {
      long count = numArg(tok, n, 2, 50);
      long gap = numArg(tok, n, 3, 2000);
      if (count < 1 || count > 5000 || gap < 100 || gap > 600000) {
        Serial.println("[TEST] usage: test hb [count=50] [gap_ms=2000]");
        return true;
      }
      run.total = (int)count; run.gapMs = (uint32_t)gap;
      testStart(T_HB);
      run.nextMs = millis();
      testEvent("TEST_START,hb,%ld,%ld", count, gap);
    } else {
      testHelp();
    }
    return true;
  }

  if (!strcmp(c, "block")) {
    if (n >= 2 && !strcmp(tok[1], "none")) memset(blocked, 0, sizeof(blocked));
    for (int i = 1; i < n; i++) if (strcmp(tok[i], "none")) addBlocked(atoi(tok[i]));
    if (n >= 2) saveBlocked();
    printCfg();
    return true;
  }
  if (!strcmp(c, "unblock")) {
    for (int i = 1; i < n; i++) {
      int id = atoi(tok[i]);
      for (int j = 0; j < TEST_MAX_BLOCK; j++) if (blocked[j] == id) blocked[j] = 0;
    }
    if (n >= 2) saveBlocked();
    printCfg();
    return true;
  }
  if (!strcmp(c, "quiet")) return onOff(tok, n, &testQuiet, "quiet");
  if (!strcmp(c, "mute"))  return onOff(tok, n, &testMute, "mute");
  if (!strcmp(c, "lbt"))   return onOff(tok, n, &testLbtEnabled, "lbt");
  if (!strcmp(c, "trace")) return onOff(tok, n, &testTrace, "trace");
  if (!strcmp(c, "cfg") || !strcmp(c, "topo")) { printCfg(); return true; }
  if (!strcmp(c, "stats")) { printStats(); return true; }
  if (!strcmp(c, "testhelp")) { testHelp(); return true; }
  return false;
}

void testInit() {
  memset(&run, 0, sizeof(run));
  loadBlocked();
  printCfg();
  Serial.println("[TEST] mesh test build: type 'testhelp' for the test commands");
}
