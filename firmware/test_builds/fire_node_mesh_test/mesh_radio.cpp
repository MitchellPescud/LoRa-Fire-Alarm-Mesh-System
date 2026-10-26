#include <Arduino.h>
#include "LoRaWan_APP.h"
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "config.h"
#include "mesh_test.h"

RadioStats radioStats = {0, 0, 0, 0, 0};

static RadioEvents_t radioEvents;
static RadioRxHandler rxHandler = nullptr;

static bool     txBusy      = false;
static uint32_t txStartedMs = 0;
static const uint32_t TX_WATCHDOG_MS = 4000;   // SetTxConfig timeout is 3000 ms

// Single-slot hand-off from the RxDone callback to the main loop.
static bool    rxPending = false;
static uint8_t rxBuf[MESH_MAX_PKT];
static uint8_t rxLen = 0;
static int16_t rxRssi = 0;
static int8_t  rxSnr = 0;

struct RadioTxSlot {
  bool      used;
  uint8_t   len;
  uint8_t   prio;
  uint8_t   defers;
  uint32_t  sendAtMs;
  uint32_t  enqMs;       // when it was queued (TEST BUILD: queue-delay trace)
  uint8_t   buf[MESH_MAX_PKT];
};
static RadioTxSlot txQueue[MESH_TX_QUEUE];

// ---- Heltec driver callbacks (run inside Radio.IrqProcess(), main loop) ----
static void onTxDone() {
  txBusy = false;
  radioStats.txDone++;
}

static void onTxTimeout() {
  Radio.Sleep();
  txBusy = false;
  radioStats.txTimeout++;
}

static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size == 0 || size > MESH_MAX_PKT) return;   // not one of ours
  memcpy(rxBuf, payload, size);
  rxLen = (uint8_t)size;
  rxRssi = rssi;
  rxSnr = snr;
  rxPending = true;
}

static void onRxError() {
  radioStats.rxCrcError++;
}

static void onRxTimeout() {
  // Nothing to do: radioService() re-arms RX whenever the driver isn't in RX.
}

// LoRa sync word byte 0xXY is written to the SX126x as the register pair
// 0xX4 / 0xY4 (0x34 -> 0x3444 public, 0x12 -> 0x1424 private). Custom value =
// PHY-level filter so foreign LoRa traffic is never even demodulated.
static void applySyncWord() {
  Radio.Write(0x0740, (uint8_t)((LORA_SYNC_WORD & 0xF0) | 0x04));
  Radio.Write(0x0741, (uint8_t)(((LORA_SYNC_WORD & 0x0F) << 4) | 0x04));
}

bool radioInit() {
  memset(txQueue, 0, sizeof(txQueue));

  radioEvents.TxDone    = onTxDone;
  radioEvents.TxTimeout = onTxTimeout;
  radioEvents.RxDone    = onRxDone;
  radioEvents.RxError   = onRxError;
  radioEvents.RxTimeout = onRxTimeout;

  Radio.Init(&radioEvents);
  Radio.SetChannel(LORA_FREQ_HZ);

  Radio.SetTxConfig(MODEM_LORA, LORA_TX_POWER_DBM, 0, LORA_BW_INDEX,
                    LORA_SF, LORA_CR_INDEX, LORA_PREAMBLE_LEN,
                    false /*fixLen*/, true /*crc*/, 0, 0, false /*iq*/, 3000);

  Radio.SetRxConfig(MODEM_LORA, LORA_BW_INDEX, LORA_SF, LORA_CR_INDEX,
                    0, LORA_PREAMBLE_LEN, 0 /*symbTimeout*/, false /*fixLen*/,
                    0, true /*crc*/, 0, 0, false /*iq*/, true /*rxContinuous*/);

  applySyncWord();

  Serial.print("[RADIO] ");
  Serial.print(LORA_FREQ_HZ / 1000000.0, 1);
  Serial.print(" MHz SF");
  Serial.print(LORA_SF);
  Serial.print(" BW125 CR4/5 sync=0x");
  Serial.print(LORA_SYNC_WORD, HEX);
  Serial.print(" (regs 0x");
  Serial.print(Radio.Read(0x0740), HEX);
  Serial.print(" 0x");
  Serial.print(Radio.Read(0x0741), HEX);
  Serial.print(") txpower=");
  Serial.print(LORA_TX_POWER_DBM);
  Serial.print("dBm airtime(18B)=");
  Serial.print(Radio.TimeOnAir(MODEM_LORA, 18));
  Serial.println("ms");
  return true;
}

void radioSetRxHandler(RadioRxHandler h) { rxHandler = h; }

bool radioQueueTx(const uint8_t *buf, uint8_t len, uint32_t delayMs, RadioPrio prio) {
  if (len == 0 || len > MESH_MAX_PKT) return false;

  int slot = -1;
  for (int i = 0; i < MESH_TX_QUEUE; i++) {
    if (!txQueue[i].used) { slot = i; break; }
  }
  if (slot < 0 && prio == RADIO_PRIO_HIGH) {
    // Full: a high-priority packet may bump a queued low-priority one.
    for (int i = 0; i < MESH_TX_QUEUE; i++) {
      if (txQueue[i].prio == RADIO_PRIO_LOW) { slot = i; radioStats.txDropped++; break; }
    }
  }
  if (slot < 0) { radioStats.txDropped++; return false; }

  RadioTxSlot &s = txQueue[slot];
  s.used = true;
  s.len = len;
  s.prio = prio;
  s.defers = 0;
  s.sendAtMs = millis() + delayMs;
  s.enqMs = millis();
  memcpy(s.buf, buf, len);
  return true;
}

void radioService() {
  Radio.IrqProcess();

  if (rxPending) {
    rxPending = false;
    if (rxHandler) rxHandler(rxBuf, rxLen, rxRssi, rxSnr);
  }

  uint32_t now = millis();

  if (txBusy) {
    if (now - txStartedMs > TX_WATCHDOG_MS) {   // never got TxDone/TxTimeout
      Radio.Sleep();
      txBusy = false;
      radioStats.txTimeout++;
    }
    return;
  }

  // After TX, an RX timeout, or on first boot the driver isn't in RX: re-arm.
  // (In continuous RX the SX1262 stays in RX after each received packet.)
  if (Radio.GetStatus() != RF_RX_RUNNING) {
    Radio.Rx(0);
  }

  // Earliest due packet goes first.
  int best = -1;
  for (int i = 0; i < MESH_TX_QUEUE; i++) {
    if (!txQueue[i].used) continue;
    if ((int32_t)(now - txQueue[i].sendAtMs) < 0) continue;
    if (best < 0 || (int32_t)(txQueue[i].sendAtMs - txQueue[best].sendAtMs) < 0) best = i;
  }
  if (best < 0) return;

  RadioTxSlot &s = txQueue[best];

  // Listen-before-talk: if the channel is busy, back off a little (bounded).
  if (testLbtEnabled && s.defers < LBT_MAX_DEFERS && Radio.GetStatus() == RF_RX_RUNNING) {
    int16_t rssi = Radio.Rssi(MODEM_LORA);
    if (rssi > LBT_RSSI_DBM) {
      s.defers++;
      s.sendAtMs = now + random(LBT_BACKOFF_MIN_MS, LBT_BACKOFF_MAX_MS);
      radioStats.txDeferred++;
      return;
    }
  }

  uint8_t out[MESH_MAX_PKT];
  uint8_t len = s.len;
  memcpy(out, s.buf, len);
  uint32_t queuedMs = now - s.enqMs;     // wait in the queue: relay delay + listen-before-talk backoffs
  uint8_t  defers = s.defers;
  s.used = false;

  testStampLane(out);                     // TEST BUILD: say who is transmitting this frame
  Radio.Send(out, len);
  txBusy = true;
  txStartedMs = millis();
  testTraceTx(out, len, queuedMs, defers, txStartedMs);
}
