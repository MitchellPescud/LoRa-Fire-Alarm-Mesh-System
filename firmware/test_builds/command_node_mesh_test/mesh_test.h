#ifndef MESH_TEST_H
#define MESH_TEST_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// TEST BUILD ONLY (Fire_Node_v5_Test / Fire_Command_v2_Test).
//
// Adds measurement tools around the unchanged V5 mesh so that "nodes talking
// and relaying" can be measured and written up:
//
//   1. Structured trace lines on Serial (one per packet sent / heard, plus
//      protocol events). Tags: TX, RX, EV, CFG, STAT, RES. mesh_logger.py
//      timestamps them on the PC and analyze_mesh.py turns them into tables
//      and plots.
//   2. "Immediate sender" lane. Byte [3] of every packet carries the TTL in
//      its low 3 bits (max 5 is used) and, in this build, the sender's node
//      id (low 5 bits of it) in the upper 5 bits. It is outside the HMAC
//      tag, like the TTL. A receiver can therefore tell WHO it heard (not
//      just who originated the packet), which gives per-link statistics and
//      lets `block` emulate nodes that are out of range of each other.
//      Test nodes do NOT interoperate with normal V5 nodes - flash all three.
//   3. Software topology. `block <id>` drops every frame sent directly by
//      that node, so three boards on a desk behave as a line A - B - C.
//   4. Automatic tests started from the serial terminal: `test ping`,
//      `test alarm`, `test hb` (see mesh_test.cpp for the full list).
// ---------------------------------------------------------------------------

extern bool testTrace;         // print TX / RX / EV lines
extern bool testQuiet;         // stop periodic heartbeats from this node
extern bool testMute;          // buzzer muted during alarms (default on)
extern bool testLbtEnabled;    // listen-before-talk (default on)

void testInit();                                   // load saved topology, print CFG
void testService();                                // call every loop() pass
bool testHandleSerial(const char *line);           // true if the line was a test command

// ---- hooks used by the mesh / radio code ----
bool testLaneBlocked(uint8_t lane);                // true: frame came directly from a blocked node
void testStampLane(uint8_t *pkt);                  // writes this node's lane into byte [3]
void testTraceTx(const uint8_t *pkt, uint8_t len, uint32_t queueMs, uint8_t defers, uint32_t atMs);
void testTraceRx(const uint8_t *raw, uint8_t len, int16_t rssi, int8_t snr, uint8_t hops, const char *disp);
void testEvent(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
