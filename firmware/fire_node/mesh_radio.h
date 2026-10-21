#ifndef MESH_RADIO_H
#define MESH_RADIO_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Thin wrapper over Heltec's native radio driver (LoRaWan_APP.h). Owns the
// SX1262 state machine: continuous RX, a small prioritised TX queue with
// per-item send times (for relay jitter), and listen-before-talk.
//
// Everything runs from radioService() in the main loop - Heltec's driver
// only sets a flag in the DIO1 interrupt; the callbacks run inside
// Radio.IrqProcess(), which radioService() calls, so no locking is needed.
// ---------------------------------------------------------------------------

typedef void (*RadioRxHandler)(const uint8_t *buf, uint8_t len, int16_t rssi, int8_t snr);

enum RadioPrio : uint8_t { RADIO_PRIO_LOW = 0, RADIO_PRIO_HIGH = 1 };

struct RadioStats {
  uint32_t txDone;
  uint32_t txTimeout;
  uint32_t txDropped;     // queue full
  uint32_t txDeferred;    // listen-before-talk backoffs
  uint32_t rxCrcError;
};
extern RadioStats radioStats;

// Call Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE) first (done in the .ino).
bool radioInit();
void radioSetRxHandler(RadioRxHandler h);

// Queue a packet to go out `delayMs` from now. Returns false if dropped.
bool radioQueueTx(const uint8_t *buf, uint8_t len, uint32_t delayMs, RadioPrio prio);

void radioService();

#endif
