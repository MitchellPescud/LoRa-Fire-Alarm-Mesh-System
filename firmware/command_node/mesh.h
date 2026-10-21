#ifndef MESH_H
#define MESH_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Mesh networking layer: managed flooding, authenticated FIRE / CLEAR /
// SILENCE, heartbeats and the alarm-episode state machine.
//
// Alarms are tracked as episodes: alarmId = (originNode, episode). A node's
// alarm is "active" while at least one episode is in its table; the buzzer
// is silenced when every active episode is silenced. Episodes end only when a
// CLEAR for them is received (or issued locally) - they never time out, so a
// destroyed origin cannot make the rest of the network forget a fire.
//
// Packet handling order (see onRadioRx in mesh.cpp):
//   magic -> HMAC -> own-echo -> replay/duplicate window -> cleared check
//   -> act -> relay
// ---------------------------------------------------------------------------

struct MeshStats {
  uint32_t rxTotal;
  uint32_t rxBadMagic;     // other networks sharing the channel
  uint32_t rxMalformed;    // bad length / version / type
  uint32_t rxAuthFail;     // HMAC mismatch (wrong key or forged)
  uint32_t rxOwnEcho;      // our own packet relayed back to us
  uint32_t rxDup;          // flood duplicates dropped
  uint32_t rxReplay;       // older than the replay window, dropped
  uint32_t rxClearedDrop;  // FIRE for an already-cleared episode, dropped
  uint32_t rxPeerFull;     // peer table full, packet dropped
  uint32_t rxFire, rxHeartbeat, rxClear, rxSilence;
  uint32_t txOriginated;
  uint32_t txRelayed;
  int16_t  lastRssi;
  int8_t   lastSnr;
};
extern MeshStats meshStats;

void meshInit();       // loads NVS counters, brings up the radio. Needs Mcu.begin() first.
void meshService();    // call every loop() pass (and inside long blocking waits)

// ---- Local events (called from alarm.cpp) ----
void meshStartLocalEpisode();   // local smoke confirmed / manual test: new episode + FIRE flood
void meshLocalClearAll();       // operator / auto clear: CLEAR flood for every active alarm
void meshLocalSilence();        // operator silence: SILENCE flood, buzzers off network-wide

// Targeted control, used by the command node's per-node buttons. `origin` is the
// node whose alarm(s) to act on; MESH_ALL_ORIGINS (0) means every active alarm.
// Only alarms this node currently knows about (has heard FIRE for) can be acted on.
// Return the number of alarms affected.
#define MESH_ALL_ORIGINS 0
int  meshClearOrigin(uint8_t origin);
int  meshSilenceOrigin(uint8_t origin);

// ---- Queries ----
bool    meshLocalEpisodeActive();
bool    meshRearmHeld();                 // true during the post-CLEAR hold-off on our own sensor
int     meshActiveAlarmCount();
uint8_t meshFirstAlarmOrigin();          // origin node of the oldest active alarm (0 if none)
int     meshPeerCount();                 // peers heard recently (excludes this node)

// Fragment for the dashboard's /status JSON: "mesh":{...}  (no surrounding braces/commas)
String  meshStatusJson();
void    meshPrintStatus();               // serial "mesh" command

#endif
