#include "state.h"

bool fireActive   = false;
bool alarmLatched = false;

bool  bmpOk            = false;
float lastTemp         = NAN;
int   batteryPercent   = -1;
bool  usbPowerDetected = true;    // always USB powered: heartbeats report "external power" so other dashboards show it
int   dustRawLast      = 0;
int   dustBaseline     = 0;
bool  baselineReady    = false;
bool  smokePresent     = false;
int   highReadingCount = 0;
int   lowReadingCount  = 0;
