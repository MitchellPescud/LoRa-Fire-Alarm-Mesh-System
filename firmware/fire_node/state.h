#ifndef STATE_H
#define STATE_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Shared cross-module state. Each block is "owned" (written) by one .cpp
// file, but read by others. Timing/bookkeeping variables that only their
// owning module needs (like lastDustMs, the pulse-train ring buffer, buzzer
// chirp timers, etc.) are kept as file-local statics inside that module
// instead of cluttering this file - only put something here if more than
// one .cpp needs to see it.
// ---------------------------------------------------------------------------

// ---- Temp sensor (owned by temp_sensor.cpp) ----
extern bool  bmpOk;
extern float lastTemp;

// ---- Battery (owned by battery.cpp) ----
extern int   batteryPercent;
extern float batteryVoltage;          // FILTERED (V6) - what everything else should use
extern float batteryVoltageInstant;   // unfiltered burst, diagnostics only

// ---- Power source (owned by dust_sensor.cpp, set once at boot) ----
extern bool  usbPowerDetected;   // true if external 5V was present on PIN_FIVE_V_SENSE at boot
extern bool  usbVoltageLow;      // true if external 5V is present but below FIVE_V_LOW_WARNING_THRESHOLD
extern float fiveVRailVoltage;   // estimated actual 5V rail voltage (sense pin reading x2)

// ---- Dust sensor / smoke detection (owned by dust_sensor.cpp) ----
extern int  dustPercent;
extern int  dustRawLast;
extern int  dustBaseline;
extern bool baselineReady;
extern int  highReadingCount;
extern int  lowReadingCount;
extern bool smokePresent;
extern float calibrationVbat;  // VBAT recorded at baseline calibration - see dust_sensor.cpp

// ---- Alarm (owned by alarm.cpp) ----
extern bool fireActive;
extern bool alarmLatched;

// ---- WiFi / dashboard (owned by wifi_dashboard.cpp) ----
extern bool wifiApActive;

#endif
