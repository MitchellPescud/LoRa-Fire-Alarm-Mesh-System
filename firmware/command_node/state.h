#ifndef STATE_H
#define STATE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Shared state. The command node has no sensors, so most of this exists only so
// that mesh.cpp and alarm.cpp can be IDENTICAL to the fire node's copies (they
// are covered by the PC simulator in Fire_Node_v4/tests). The unused values
// below are fixed inert defaults.
// ---------------------------------------------------------------------------

// ---- Alarm (owned by alarm.cpp) ----
extern bool fireActive;      // true while any alarm (from any node) is active
extern bool alarmLatched;

// ---- Unused on the command node (fixed defaults, see state.cpp) ----
extern bool  bmpOk;              // no temperature sensor
extern float lastTemp;
extern int   batteryPercent;     // -1 = no battery reading -> heartbeat reports "n/a"
extern bool  usbPowerDetected;
extern int   dustRawLast;
extern int   dustBaseline;
extern bool  baselineReady;
extern bool  smokePresent;
extern int   highReadingCount;
extern int   lowReadingCount;

#endif
