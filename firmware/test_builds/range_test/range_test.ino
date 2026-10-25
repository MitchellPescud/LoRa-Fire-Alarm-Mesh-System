/*
  ============================================================================
  Range Test v1 - single-hop LoRa link measurement
  Board: Heltec WiFi LoRa 32 V4.3 (ESP32-S3 + SX1262 + KCT8103L front-end)
  ============================================================================

  Standalone sketch for the CMPE3002 range experiments. It deliberately has NO
  mesh, relaying, retries, ACK logic or HMAC, so every number it reports is a
  raw single-hop packet delivery measurement. Flash the SAME sketch on two
  boards. Either board can transmit, and both always log what they receive.

  How a measurement works
    * A "burst" is N numbered probe packets (default 100) sent at a fixed
      interval. Every burst is tagged with a POINT number (1, 2, 3 ... counts up
      on each burst) so you can match logs to where you were standing.
    * The receiver logs one CSV line per probe (seq, RSSI, SNR) and, after the
      burst, a SUM line with packets received / expected, PDR, mean RSSI, SNR.
    * The receiver then sends a short REPORT back. The sender prints/shows it, so
      at the dwell point you immediately see "92/100 -112 dBm" and the reply's
      own RSSI/SNR gives you the REVERSE direction for free.
    * No retries anywhere: a lost probe stays lost.

  Typical use
    1. Laptop on the fixed ("logger") node with a serial monitor logging to a
       file (115200 baud). Antenna vertical, fixed height.
    2. Walk away with the mobile node on a pole. At each dwell point press the
       PRG button (short press): it sends the burst, then shows the result.
    3. Write down point number -> location/distance. After the walk, run
       analyze_range.py on the logged file.

  Button (PRG / GPIO0):  short press = start burst (press again = cancel)
                         hold 1.5 s  = switch BOTH nodes between RT_SF and
                                       RT_SF_ALT (broadcast, see below)
  Buzzer feedback after a burst (if a buzzer is on RT_PIN_BUZZER):
                         2 short beeps = PDR >= 90 %, 1 short = 50-89 %,
                         1 long = below 50 % or no report

  Serial commands (115200, newline):
    go [count] [intervalMs]   start a burst (count 1..1000)
    point N                   set the point number used by the NEXT burst
    sf N | pwr N | bw N       change THIS node only (sf 7-12, pwr dBm, bw 0=125 1=250 2=500 kHz)
    setall sf=N [pwr=N] [bw=N]  change BOTH nodes: broadcasts the change (3 copies) at the
                              current settings, then switches this node. Only do this
                              while the link still works!
    len N                     probe length in bytes (16..64)
    noise                     sample idle RSSI for 2 s (noise floor)
    status | help
  Reset either board and it comes back up on the compile-time defaults below,
  which is the recovery if the two nodes ever end up on different settings.

  Log line formats (all begin with a tag, easy to filter):
    RX,ms,run,seq,total,point,src,rssi,snr,sf,bw_khz,pwr,len
    SUM,ms,run,point,src,total,received,pdr_pct,avg_rssi,min_rssi,avg_snr,min_snr,sf,bw_khz,pwr
    TX,ms,run,point,total,sf,bw_khz,pwr,len,airtime_ms,interval_ms
    TXDONE,ms,run,point,sent
    REP,ms,run,point,total,received,pdr_pct,rep_avg_rssi,rep_min_rssi,rep_avg_snr,link_rssi,link_snr
    REPNONE,ms,run,point
    NOISE,ms,samples,avg_rssi,min_rssi,max_rssi,sf,bw_khz
    CFG,ms,sf,bw_khz,pwr,src

  Notes
    * Heltec's driver reports SNR in whole dB (1 dB resolution).
    * SF11/SF12 at 125 kHz use low-data-rate optimisation; Heltec's SX126x
      driver enables it automatically for that combination on both sides.
    * No listen-before-talk here on purpose (it would add random delays).
    * Probe interval is raised automatically to at least airtime + 120 ms.
    * Different sync word (RT_SYNC_WORD) from the fire mesh, so a mesh node in
      the same room will not decode these packets (and vice versa).
    * Keep TX power at the value you intend to deploy (10 dBm) unless you are
      deliberately lowering it to reach the link edge.
    * Never transmit without an antenna attached.

  Arduino IDE (Heltec ESP32 board package): Board = WiFi LoRa 32(V4); Tools ->
  LoRa FEM = KCT8103L; USB CDC On Boot enabled.
  ============================================================================
*/

#include <Arduino.h>
#include "LoRaWan_APP.h"

// ---------------------------------------------------------------------------
// Settings (compile-time defaults; boot always returns to these)
// ---------------------------------------------------------------------------
#ifndef NODE_ADDR
#define NODE_ADDR           1            // give each board a different value (1..250)
#endif

#define RT_FREQ_HZ          921000000UL  // same channel as the fire mesh
#define RT_SF               9            // main test SF
#define RT_SF_ALT           11           // second SF, toggled by the long button press
#define RT_BW_INDEX         0            // 0 = 125 kHz, 1 = 250, 2 = 500
#define RT_CR_INDEX         1            // 4/5
#define RT_PREAMBLE         8
#define RT_SYNC_WORD        0x5B         // NOT the mesh's 0x5A, NOT 0x12 / 0x34
#define RT_TX_POWER_DBM     10
#define RT_TX_POWER_MAX     20           // hard cap, same as the fire mesh config

#define RT_BURST_COUNT      100          // probes per burst
#define RT_INTERVAL_MS      500          // requested gap between probe starts
#define RT_MIN_GAP_MS       120          // extra margin above airtime
#define RT_PAYLOAD_LEN      18           // bytes on air (same as a mesh FIRE packet)
#define RT_MAX_COUNT        1000

#define RT_PIN_BUTTON       0            // PRG button, active LOW
#define RT_PIN_BUZZER       15           // active HIGH; set -1 if nothing is wired here
#define RT_PIN_VEXT         36           // LOW = Vext on (powers the OLED)
#define RT_USE_OLED         1

#if RT_TX_POWER_DBM > RT_TX_POWER_MAX
#error "RT_TX_POWER_DBM exceeds RT_TX_POWER_MAX"
#endif

#if RT_USE_OLED
#include <HT_SSD1306Wire.h>
extern SSD1306Wire display;              // Heltec library's global, do not declare a second one
#endif

// ---------------------------------------------------------------------------
// Wire format (all multi-byte fields little-endian)
//   0-1 'R''T'  2 type  3 src node
//   PROBE : 4-5 run, 6-7 seq, 8-9 total, 10 point, 11 sf, 12 pwr(int8), 13 bw,
//           14-15 intervalMs, 16.. padding
//   REPORT: 4-5 run, 6-7 received, 8-9 total, 10 point, 11-12 avgRssi(int16),
//           13 avgSnr(int8), 14-15 minRssi(int16)
//   CFG   : 4 sf, 5 pwr(int8), 6 bw
// ---------------------------------------------------------------------------
static const uint8_t T_PROBE = 1, T_REPORT = 2, T_CFG = 3;
static const uint8_t HDR_PROBE = 16, LEN_REPORT = 16, LEN_CFG = 7;
static const uint8_t MAX_PKT = 64;

static inline void wr16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

// ---------------------------------------------------------------------------
// Radio state
// ---------------------------------------------------------------------------
static RadioEvents_t radioEvents;
static volatile bool txBusy = false;
static uint32_t txStartedMs = 0;

static bool    rxPending = false;
static uint8_t rxBuf[MAX_PKT];
static uint8_t rxLen = 0;
static int16_t rxRssi = 0;
static int8_t  rxSnr = 0;
static uint32_t rxCrcErrors = 0;

static uint8_t curSf  = RT_SF;
static int8_t  curPwr = RT_TX_POWER_DBM;
static uint8_t curBw  = RT_BW_INDEX;
static uint8_t curLen = RT_PAYLOAD_LEN;
static bool    needRxArm = true;

static uint16_t bwKhz(uint8_t idx) { return idx == 0 ? 125 : (idx == 1 ? 250 : 500); }

static void onTxDone()    { txBusy = false; }
static void onTxTimeout() { Radio.Sleep(); txBusy = false; }
static void onRxError()   { rxCrcErrors++; }
static void onRxTimeout() { }
static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size == 0 || size > MAX_PKT) return;
  memcpy(rxBuf, payload, size);
  rxLen = (uint8_t)size;
  rxRssi = rssi;
  rxSnr = snr;
  rxPending = true;
}

static void applySyncWord() {
  Radio.Write(0x0740, (uint8_t)((RT_SYNC_WORD & 0xF0) | 0x04));
  Radio.Write(0x0741, (uint8_t)(((RT_SYNC_WORD & 0x0F) << 4) | 0x04));
}

static void radioConfigure() {
  Radio.Standby();
  Radio.SetChannel(RT_FREQ_HZ);
  Radio.SetTxConfig(MODEM_LORA, curPwr, 0, curBw, curSf, RT_CR_INDEX, RT_PREAMBLE,
                    false, true, 0, 0, false, 3000);
  Radio.SetRxConfig(MODEM_LORA, curBw, curSf, RT_CR_INDEX, 0, RT_PREAMBLE, 0,
                    false, 0, true, 0, 0, false, true);
  applySyncWord();
  needRxArm = true;
}

static void applyConfig(int sf, int pwr, int bw) {
  if (sf < 7) sf = 7;
  if (sf > 12) sf = 12;
  if (pwr < -9) pwr = -9;
  if (pwr > RT_TX_POWER_MAX) pwr = RT_TX_POWER_MAX;
  if (bw < 0) bw = 0;
  if (bw > 2) bw = 2;
  curSf = (uint8_t)sf;
  curPwr = (int8_t)pwr;
  curBw = (uint8_t)bw;
  radioConfigure();
}

static void sendRaw(uint8_t *buf, uint8_t len) {
  Radio.Send(buf, len);
  txBusy = true;
  txStartedMs = millis();
}

// ---------------------------------------------------------------------------
// Sender state
// ---------------------------------------------------------------------------
enum Mode : uint8_t { M_IDLE, M_BURST, M_WAIT_REPORT, M_CFG_SEND };
static Mode mode = M_IDLE;

static uint8_t  nextPoint = 1;
static uint16_t myRun = 0, lastRun = 0;
static uint8_t  myPoint = 0;
static uint16_t burstTotal = 0, burstSeq = 0;
static uint32_t effInterval = RT_INTERVAL_MS;
static uint32_t nextTxAt = 0;
static uint32_t reportDeadline = 0;
static uint16_t reqInterval = RT_INTERVAL_MS;

// last result, for the OLED
static char lastResult[40] = "no result yet";

// CFG broadcast
static uint8_t cfgCopies = 0;
static uint32_t cfgNextAt = 0;
static uint8_t cfgSf, cfgBw;
static int8_t  cfgPwr;

// ---------------------------------------------------------------------------
// Receiver session
// ---------------------------------------------------------------------------
struct RxSession {
  bool     active;
  uint16_t runId, total, count;
  uint8_t  point, src, sf, bw;
  int8_t   pwr;
  uint32_t idleMs, lastMs;
  int32_t  sumRssi, sumSnr;
  int16_t  minRssi, maxRssi;
  int8_t   minSnr;
  uint8_t  seen[(RT_MAX_COUNT + 7) / 8];
};
static RxSession sess;

static uint8_t  repBuf[LEN_REPORT];
static uint8_t  repCopies = 0;
static uint32_t repNextAt = 0;

static int16_t lastRxRssi = 0;
static int8_t  lastRxSnr = 0;
static uint32_t lastRxMs = 0;
static bool     everRx = false;

// ---------------------------------------------------------------------------
// Feedback
// ---------------------------------------------------------------------------
static void beepMs(uint16_t ms) {
  if (RT_PIN_BUZZER < 0) return;
  digitalWrite(RT_PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(RT_PIN_BUZZER, LOW);
}
static void beepForPdr(int pdrPct) {
  if (RT_PIN_BUZZER < 0) return;
  if (pdrPct >= 90) { beepMs(70); delay(90); beepMs(70); }
  else if (pdrPct >= 50) { beepMs(70); }
  else { beepMs(400); }
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static void printHeader() {
  Serial.println(F("# RangeTest v1  tags: RX,SUM,TX,TXDONE,REP,REPNONE,NOISE,CFG"));
  Serial.println(F("# RX,ms,run,seq,total,point,src,rssi,snr,sf,bw_khz,pwr,len"));
  Serial.println(F("# SUM,ms,run,point,src,total,received,pdr_pct,avg_rssi,min_rssi,avg_snr,min_snr,sf,bw_khz,pwr"));
  Serial.println(F("# TX,ms,run,point,total,sf,bw_khz,pwr,len,airtime_ms,interval_ms"));
  Serial.println(F("# REP,ms,run,point,total,received,pdr_pct,rep_avg_rssi,rep_min_rssi,rep_avg_snr,link_rssi,link_snr"));
}

static void printStatus() {
  Serial.print(F("# node ")); Serial.print(NODE_ADDR);
  Serial.print(F(" SF")); Serial.print(curSf);
  Serial.print(F(" BW")); Serial.print(bwKhz(curBw));
  Serial.print(F(" pwr=")); Serial.print(curPwr);
  Serial.print(F("dBm len=")); Serial.print(curLen);
  Serial.print(F(" airtime=")); Serial.print(Radio.TimeOnAir(MODEM_LORA, curLen));
  Serial.print(F("ms nextPoint=")); Serial.print(nextPoint);
  Serial.print(F(" crcErrors=")); Serial.println(rxCrcErrors);
}

// ---------------------------------------------------------------------------
// Receiver logic
// ---------------------------------------------------------------------------
static void finalizeSession() {
  if (!sess.active) return;
  sess.active = false;

  int pdr = sess.total ? (int)((sess.count * 100UL + sess.total / 2) / sess.total) : 0;
  float avgRssi = sess.count ? (float)sess.sumRssi / sess.count : 0;
  float avgSnr  = sess.count ? (float)sess.sumSnr / sess.count : 0;

  Serial.print(F("SUM,")); Serial.print(millis());
  Serial.print(','); Serial.print(sess.runId);
  Serial.print(','); Serial.print(sess.point);
  Serial.print(','); Serial.print(sess.src);
  Serial.print(','); Serial.print(sess.total);
  Serial.print(','); Serial.print(sess.count);
  Serial.print(','); Serial.print(pdr);
  Serial.print(','); Serial.print(avgRssi, 1);
  Serial.print(','); Serial.print(sess.minRssi);
  Serial.print(','); Serial.print(avgSnr, 1);
  Serial.print(','); Serial.print(sess.minSnr);
  Serial.print(','); Serial.print(sess.sf);
  Serial.print(','); Serial.print(bwKhz(sess.bw));
  Serial.print(','); Serial.println(sess.pwr);

  snprintf(lastResult, sizeof(lastResult), "IN p%u %u/%u %ddBm",
           sess.point, sess.count, sess.total, (int)(avgRssi - (avgRssi < 0 ? 0.5f : -0.5f)));

  // Reply to the sender, twice (copies spaced ~700 ms) so one collision/fade does not lose it.
  repBuf[0] = 'R'; repBuf[1] = 'T'; repBuf[2] = T_REPORT; repBuf[3] = NODE_ADDR;
  wr16(repBuf + 4, sess.runId);
  wr16(repBuf + 6, sess.count);
  wr16(repBuf + 8, sess.total);
  repBuf[10] = sess.point;
  wr16(repBuf + 11, (uint16_t)(int16_t)(avgRssi - (avgRssi < 0 ? 0.5f : -0.5f)));
  repBuf[13] = (uint8_t)(int8_t)(avgSnr - (avgSnr < 0 ? 0.5f : -0.5f));
  wr16(repBuf + 14, (uint16_t)sess.minRssi);
  repCopies = 2;
  repNextAt = millis() + 250 + (uint32_t)random(0, 150);
}

static void startSession(uint16_t run, uint16_t total, uint8_t point, uint8_t src,
                         uint8_t sf, int8_t pwr, uint8_t bw, uint16_t intervalMs) {
  memset(&sess, 0, sizeof(sess));
  sess.active = true;
  sess.runId = run;
  sess.total = total;
  sess.point = point;
  sess.src = src;
  sess.sf = sf;
  sess.pwr = pwr;
  sess.bw = bw;
  sess.minRssi = 32767;
  sess.maxRssi = -32768;
  sess.minSnr = 127;
  uint32_t idle = 3UL * intervalMs + 500;
  sess.idleMs = idle < 2500 ? 2500 : idle;
}

static void onProbe(const uint8_t *b, uint8_t len, int16_t rssi, int8_t snr) {
  if (len < HDR_PROBE) return;
  uint16_t run = rd16(b + 4), seq = rd16(b + 6), total = rd16(b + 8);
  if (total == 0 || total > RT_MAX_COUNT || seq >= total) return;
  if (mode == M_BURST) return;   // we are transmitting our own burst

  if (!sess.active || sess.runId != run) {
    if (sess.active) finalizeSession();
    startSession(run, total, b[10], b[3], b[11], (int8_t)b[12], b[13], rd16(b + 14));
  }
  if (sess.seen[seq >> 3] & (1 << (seq & 7))) return;   // duplicate
  sess.seen[seq >> 3] |= (1 << (seq & 7));

  sess.count++;
  sess.sumRssi += rssi;
  sess.sumSnr += snr;
  if (rssi < sess.minRssi) sess.minRssi = rssi;
  if (rssi > sess.maxRssi) sess.maxRssi = rssi;
  if (snr < sess.minSnr) sess.minSnr = snr;
  sess.lastMs = millis();

  Serial.print(F("RX,")); Serial.print(millis());
  Serial.print(','); Serial.print(run);
  Serial.print(','); Serial.print(seq);
  Serial.print(','); Serial.print(total);
  Serial.print(','); Serial.print(b[10]);
  Serial.print(','); Serial.print(b[3]);
  Serial.print(','); Serial.print(rssi);
  Serial.print(','); Serial.print(snr);
  Serial.print(','); Serial.print(b[11]);
  Serial.print(','); Serial.print(bwKhz(b[13]));
  Serial.print(','); Serial.print((int8_t)b[12]);
  Serial.print(','); Serial.println(len);

  lastRxRssi = rssi; lastRxSnr = snr; lastRxMs = millis(); everRx = true;

  if (sess.count == sess.total) finalizeSession();
}

static void onReport(const uint8_t *b, uint8_t len, int16_t rssi, int8_t snr) {
  if (len < LEN_REPORT) return;
  if (mode != M_WAIT_REPORT) return;
  uint16_t run = rd16(b + 4);
  if (run != myRun) return;

  uint16_t got = rd16(b + 6), total = rd16(b + 8);
  int16_t avgRssi = (int16_t)rd16(b + 11);
  int8_t avgSnr = (int8_t)b[13];
  int16_t minRssi = (int16_t)rd16(b + 14);
  int pdr = total ? (int)((got * 100UL + total / 2) / total) : 0;

  Serial.print(F("REP,")); Serial.print(millis());
  Serial.print(','); Serial.print(run);
  Serial.print(','); Serial.print(b[10]);
  Serial.print(','); Serial.print(total);
  Serial.print(','); Serial.print(got);
  Serial.print(','); Serial.print(pdr);
  Serial.print(','); Serial.print(avgRssi);
  Serial.print(','); Serial.print(minRssi);
  Serial.print(','); Serial.print(avgSnr);
  Serial.print(','); Serial.print(rssi);
  Serial.print(','); Serial.println(snr);

  snprintf(lastResult, sizeof(lastResult), "OUT p%u %u/%u %ddBm", b[10], got, total, avgRssi);
  mode = M_IDLE;
  beepForPdr(pdr);
}

static void onCfg(const uint8_t *b, uint8_t len) {
  if (len < LEN_CFG) return;
  if (mode == M_BURST || mode == M_CFG_SEND) return;
  if (b[4] == curSf && (int8_t)b[5] == curPwr && b[6] == curBw) return;   // already there (later copies)
  applyConfig(b[4], (int8_t)b[5], b[6]);
  Serial.print(F("CFG,")); Serial.print(millis());
  Serial.print(','); Serial.print(curSf);
  Serial.print(','); Serial.print(bwKhz(curBw));
  Serial.print(','); Serial.print(curPwr);
  Serial.print(','); Serial.println(b[3]);
}

static void handleRx(const uint8_t *b, uint8_t len, int16_t rssi, int8_t snr) {
  if (len < 4 || b[0] != 'R' || b[1] != 'T') return;
  if (b[3] == NODE_ADDR) return;
  switch (b[2]) {
    case T_PROBE:  onProbe(b, len, rssi, snr); break;
    case T_REPORT: onReport(b, len, rssi, snr); break;
    case T_CFG:    onCfg(b, len); break;
  }
}

// ---------------------------------------------------------------------------
// Sender logic
// ---------------------------------------------------------------------------
static void startBurst(uint16_t count, uint16_t intervalMs) {
  if (mode == M_CFG_SEND) return;
  if (count < 1) count = 1;
  if (count > RT_MAX_COUNT) count = RT_MAX_COUNT;
  if (sess.active) finalizeSession();

  do { myRun = (uint16_t)(esp_random() & 0xFFFF); } while (myRun == 0 || myRun == lastRun);
  lastRun = myRun;
  myPoint = nextPoint++;
  burstTotal = count;
  burstSeq = 0;
  reqInterval = intervalMs;

  uint32_t air = Radio.TimeOnAir(MODEM_LORA, curLen);
  effInterval = intervalMs;
  if (effInterval < air + RT_MIN_GAP_MS) effInterval = air + RT_MIN_GAP_MS;
  nextTxAt = millis() + 50;
  mode = M_BURST;

  Serial.print(F("TX,")); Serial.print(millis());
  Serial.print(','); Serial.print(myRun);
  Serial.print(','); Serial.print(myPoint);
  Serial.print(','); Serial.print(count);
  Serial.print(','); Serial.print(curSf);
  Serial.print(','); Serial.print(bwKhz(curBw));
  Serial.print(','); Serial.print(curPwr);
  Serial.print(','); Serial.print(curLen);
  Serial.print(','); Serial.print(air);
  Serial.print(','); Serial.println(effInterval);

  snprintf(lastResult, sizeof(lastResult), "sending p%u ...", myPoint);
  beepMs(40);
}

static void sendProbe() {
  uint8_t pkt[MAX_PKT];
  memset(pkt, 0xA5, sizeof(pkt));
  pkt[0] = 'R'; pkt[1] = 'T'; pkt[2] = T_PROBE; pkt[3] = NODE_ADDR;
  wr16(pkt + 4, myRun);
  wr16(pkt + 6, burstSeq);
  wr16(pkt + 8, burstTotal);
  pkt[10] = myPoint;
  pkt[11] = curSf;
  pkt[12] = (uint8_t)curPwr;
  pkt[13] = curBw;
  wr16(pkt + 14, (uint16_t)effInterval);
  sendRaw(pkt, curLen);
  burstSeq++;
}

static void startCfgBroadcast(int sf, int pwr, int bw) {
  if (mode == M_BURST) { Serial.println(F("# busy: burst running")); return; }
  if (sf < 7) sf = 7;
  if (sf > 12) sf = 12;
  if (pwr < -9) pwr = -9;
  if (pwr > RT_TX_POWER_MAX) pwr = RT_TX_POWER_MAX;
  if (bw < 0) bw = 0;
  if (bw > 2) bw = 2;
  cfgSf = (uint8_t)sf; cfgPwr = (int8_t)pwr; cfgBw = (uint8_t)bw;
  cfgCopies = 3;
  cfgNextAt = millis();
  mode = M_CFG_SEND;
  snprintf(lastResult, sizeof(lastResult), "switching to SF%d ...", sf);
}

static void serviceSender() {
  uint32_t now = millis();

  if (txBusy && now - txStartedMs > 4000) {   // never got TxDone/TxTimeout
    Radio.Sleep();
    txBusy = false;
  }

  // Report copies (receiver side) go out whenever the radio is free and we are not busy sending.
  if (repCopies > 0 && !txBusy && mode != M_BURST && mode != M_CFG_SEND &&
      (int32_t)(now - repNextAt) >= 0) {
    uint8_t copy[LEN_REPORT];
    memcpy(copy, repBuf, LEN_REPORT);
    sendRaw(copy, LEN_REPORT);
    repCopies--;
    repNextAt = now + 700;
    return;
  }

  switch (mode) {
    case M_BURST:
      if (txBusy) return;
      if (burstSeq >= burstTotal) {
        Serial.print(F("TXDONE,")); Serial.print(now);
        Serial.print(','); Serial.print(myRun);
        Serial.print(','); Serial.print(myPoint);
        Serial.print(','); Serial.println(burstSeq);
        uint32_t idle = 3UL * effInterval + 500;
        if (idle < 2500) idle = 2500;
        reportDeadline = now + idle + 4000;      // receiver waits `idle`, then replies
        mode = M_WAIT_REPORT;
        snprintf(lastResult, sizeof(lastResult), "p%u sent, waiting...", myPoint);
        return;
      }
      if ((int32_t)(now - nextTxAt) < 0) return;
      sendProbe();
      nextTxAt = now + effInterval;
      break;

    case M_WAIT_REPORT:
      if ((int32_t)(now - reportDeadline) >= 0) {
        Serial.print(F("REPNONE,")); Serial.print(now);
        Serial.print(','); Serial.print(myRun);
        Serial.print(','); Serial.println(myPoint);
        snprintf(lastResult, sizeof(lastResult), "p%u NO REPORT", myPoint);
        mode = M_IDLE;
        beepForPdr(0);
      }
      break;

    case M_CFG_SEND:
      if (txBusy) return;
      if (cfgCopies == 0) {
        applyConfig(cfgSf, cfgPwr, cfgBw);
        Serial.print(F("CFG,")); Serial.print(now);
        Serial.print(','); Serial.print(curSf);
        Serial.print(','); Serial.print(bwKhz(curBw));
        Serial.print(','); Serial.print(curPwr);
        Serial.print(','); Serial.println(NODE_ADDR);
        snprintf(lastResult, sizeof(lastResult), "now SF%u BW%u", curSf, bwKhz(curBw));
        mode = M_IDLE;
        return;
      }
      if ((int32_t)(now - cfgNextAt) < 0) return;
      {
        uint8_t pkt[LEN_CFG];
        pkt[0] = 'R'; pkt[1] = 'T'; pkt[2] = T_CFG; pkt[3] = NODE_ADDR;
        pkt[4] = cfgSf; pkt[5] = (uint8_t)cfgPwr; pkt[6] = cfgBw;
        sendRaw(pkt, LEN_CFG);
        cfgCopies--;
        cfgNextAt = now + 700;
      }
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Noise floor
// ---------------------------------------------------------------------------
static void measureNoise() {
  if (mode != M_IDLE) { Serial.println(F("# busy")); return; }
  if (sess.active) finalizeSession();
  if (Radio.GetStatus() != RF_RX_RUNNING) { Radio.Rx(0); delay(5); }
  int32_t sum = 0;
  int16_t mn = 32767, mx = -32768;
  const int N = 40;
  for (int i = 0; i < N; i++) {
    Radio.IrqProcess();
    int16_t r = Radio.Rssi(MODEM_LORA);
    sum += r;
    if (r < mn) mn = r;
    if (r > mx) mx = r;
    delay(50);
  }
  Serial.print(F("NOISE,")); Serial.print(millis());
  Serial.print(','); Serial.print(N);
  Serial.print(','); Serial.print((float)sum / N, 1);
  Serial.print(','); Serial.print(mn);
  Serial.print(','); Serial.print(mx);
  Serial.print(','); Serial.print(curSf);
  Serial.print(','); Serial.println(bwKhz(curBw));
}

// ---------------------------------------------------------------------------
// Serial commands
// ---------------------------------------------------------------------------
static int argValue(const String &line, const char *key, int def) {
  int i = line.indexOf(key);
  if (i < 0) return def;
  return line.substring(i + strlen(key)).toInt();
}

static void handleLine(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;

  if (line.startsWith("go")) {
    int sp1 = line.indexOf(' ');
    int count = RT_BURST_COUNT, interval = RT_INTERVAL_MS;
    if (sp1 > 0) {
      String rest = line.substring(sp1 + 1);
      rest.trim();
      count = rest.toInt();
      int sp2 = rest.indexOf(' ');
      if (sp2 > 0) interval = rest.substring(sp2 + 1).toInt();
      if (count <= 0) count = RT_BURST_COUNT;
      if (interval <= 0) interval = RT_INTERVAL_MS;
    }
    startBurst((uint16_t)count, (uint16_t)interval);
  } else if (line.startsWith("point ")) {
    int v = line.substring(6).toInt();
    if (v >= 1 && v <= 255) { nextPoint = (uint8_t)v; printStatus(); }
  } else if (line.startsWith("setall")) {
    startCfgBroadcast(argValue(line, "sf=", curSf), argValue(line, "pwr=", curPwr), argValue(line, "bw=", curBw));
  } else if (line.startsWith("sf ")) {
    applyConfig(line.substring(3).toInt(), curPwr, curBw); printStatus();
  } else if (line.startsWith("pwr ")) {
    applyConfig(curSf, line.substring(4).toInt(), curBw); printStatus();
  } else if (line.startsWith("bw ")) {
    applyConfig(curSf, curPwr, line.substring(3).toInt()); printStatus();
  } else if (line.startsWith("len ")) {
    int v = line.substring(4).toInt();
    if (v >= HDR_PROBE && v <= MAX_PKT) { curLen = (uint8_t)v; printStatus(); }
  } else if (line == "noise") {
    measureNoise();
  } else if (line == "status") {
    printStatus();
  } else {
    printHeader();
    Serial.println(F("# cmds: go [count] [intervalMs] | point N | sf N | pwr N | bw N | setall sf=N [pwr=N] [bw=N] | len N | noise | status"));
    printStatus();
  }
}

static void handleSerial() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.length()) { handleLine(buf); buf = ""; }
    } else if (buf.length() < 80) {
      buf += c;
    }
  }
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
static void handleButton() {
  static bool down = false, longFired = false;
  static uint32_t downAt = 0;
  bool pressed = (digitalRead(RT_PIN_BUTTON) == LOW);
  uint32_t now = millis();

  if (pressed && !down) {
    down = true; downAt = now; longFired = false;
  } else if (pressed && down && !longFired && now - downAt >= 1500) {
    longFired = true;
    int target = (curSf == RT_SF) ? RT_SF_ALT : RT_SF;
    startCfgBroadcast(target, curPwr, curBw);
  } else if (!pressed && down) {
    down = false;
    if (!longFired && now - downAt >= 40) {
      if (mode == M_BURST) {            // short press during a burst = cancel
        burstTotal = burstSeq;
        snprintf(lastResult, sizeof(lastResult), "p%u cancelled", myPoint);
      } else {
        startBurst(RT_BURST_COUNT, RT_INTERVAL_MS);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------
#if RT_USE_OLED
static void handleOled() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < 300) return;
  lastMs = now;

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "N" + String(NODE_ADDR) + " SF" + String(curSf) + " BW" + String(bwKhz(curBw)) +
                           " " + String(curPwr) + "dBm");

  String st;
  if (mode == M_BURST) st = "TX p" + String(myPoint) + " " + String(burstSeq) + "/" + String(burstTotal);
  else if (mode == M_WAIT_REPORT) st = "p" + String(myPoint) + " waiting report...";
  else if (mode == M_CFG_SEND) st = "switching settings...";
  else if (sess.active) st = "RX p" + String(sess.point) + " " + String(sess.count) + "/" + String(sess.total);
  else st = "idle  next p" + String(nextPoint);
  display.drawString(0, 14, st);

  display.drawString(0, 28, lastResult);

  if (everRx) {
    display.drawString(0, 42, "last rx " + String(lastRxRssi) + "dBm " + String(lastRxSnr) + "dB " +
                              String((now - lastRxMs) / 1000) + "s");
  } else {
    display.drawString(0, 42, "no packets heard yet");
  }
  display.display();
}
#endif

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);   // must precede Radio.Init()

  pinMode(RT_PIN_BUTTON, INPUT_PULLUP);
  if (RT_PIN_BUZZER >= 0) { pinMode(RT_PIN_BUZZER, OUTPUT); digitalWrite(RT_PIN_BUZZER, LOW); }

#if RT_USE_OLED
  pinMode(RT_PIN_VEXT, OUTPUT);
  digitalWrite(RT_PIN_VEXT, LOW);           // power the OLED rail
  delay(100);
  display.init();
  display.setFont(ArialMT_Plain_10);
  display.clear();
  display.drawString(0, 0, "Range Test v1");
  display.drawString(0, 14, "node " + String(NODE_ADDR));
  display.display();
#endif

  radioEvents.TxDone    = onTxDone;
  radioEvents.TxTimeout = onTxTimeout;
  radioEvents.RxDone    = onRxDone;
  radioEvents.RxError   = onRxError;
  radioEvents.RxTimeout = onRxTimeout;
  Radio.Init(&radioEvents);
  radioConfigure();

  memset(&sess, 0, sizeof(sess));
  randomSeed(esp_random());

  Serial.println();
  printHeader();
  Serial.print(F("# sync=0x")); Serial.print(RT_SYNC_WORD, HEX);
  Serial.print(F(" regs 0x")); Serial.print(Radio.Read(0x0740), HEX);
  Serial.print(F(" 0x")); Serial.println(Radio.Read(0x0741), HEX);
  printStatus();
  Serial.println(F("# ready. Short-press PRG or type 'go' to send a burst. 'help' for commands."));
}

void loop() {
  Radio.IrqProcess();

  if (rxPending) {
    rxPending = false;
    handleRx(rxBuf, rxLen, rxRssi, rxSnr);
  }

  if (!txBusy && (needRxArm || Radio.GetStatus() != RF_RX_RUNNING)) {
    Radio.Rx(0);
    needRxArm = false;
  }

  serviceSender();

  if (sess.active && mode != M_BURST && millis() - sess.lastMs > sess.idleMs) {
    finalizeSession();
  }

  handleButton();
  handleSerial();
#if RT_USE_OLED
  handleOled();
#endif
}
