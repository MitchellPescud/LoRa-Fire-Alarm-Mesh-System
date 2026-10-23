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
//
// Remote commands: CMD / ACK are flooded like CLEAR. The target executes a
// request once (de-duplicated by origin + reqId), answers every copy it hears
// with an ACK, and does not relay the CMD any further; the asker consumes the ACK.
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
  uint32_t rxCmd, rxAck;
  uint32_t rxCmdRejected;  // CMD for us refused (no replay baseline for the sender yet)
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

// ---- Remote commands (any node -> any node) ----
// meshSendCommand() floods an authenticated CMD (MESH_CMD_* in mesh_proto.h) addressed
// to `target`, repeated CTRL_REPEATS times until the target's ACK arrives. Poll the
// outcome with meshCommandState() or read the "cmds" array in meshStatusJson().
//   returns  0  queued
//            1  an identical command to that node is already pending
//           -1  bad target (0, 255 or this node) or unknown command
//           -2  tracking table full of pending requests
#define MESH_CMDSTATE_PENDING  0
#define MESH_CMDSTATE_DONE     1   // ACK received, result in *result
#define MESH_CMDSTATE_NOREPLY  2   // no ACK within CMD_ACK_WAIT_MS of the last copy
#define MESH_CMDSTATE_NONE    -1   // nothing tracked for that node/command
int meshSendCommand(uint8_t target, uint8_t cmd);
int meshCommandState(uint8_t target, uint8_t cmd, uint8_t *result);
const char *meshCommandName(uint8_t cmd);
uint8_t meshCommandFromName(const char *name);   // "fire"/"recal"/"reboot"/"status" -> code, 0 if unknown
const char *meshAckName(uint8_t result);

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
