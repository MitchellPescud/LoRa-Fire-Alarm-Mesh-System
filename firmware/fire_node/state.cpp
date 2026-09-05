#include "state.h"

bool  bmpOk    = false;
float lastTemp = NAN;

int   batteryPercent = 0;
float batteryVoltage = 0.0f;

bool usbPowerDetected = false;

int  dustPercent    = 0;
int  dustRawLast    = 0;
int  dustBaseline   = 0;
bool baselineReady  = false;
int  highReadingCount = 0;
int  lowReadingCount  = 0;
bool smokePresent   = false;
float calibrationVbat = 0.0f;

bool fireActive   = false;
bool alarmLatched = false;

bool wifiApActive = false;
