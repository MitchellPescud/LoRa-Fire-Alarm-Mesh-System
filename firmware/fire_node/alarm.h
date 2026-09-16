#ifndef ALARM_H
#define ALARM_H

void alarmInit();
void handleFireState();

// Called by dust_sensor.cpp once sustained smoke is confirmed, and by
// serial_commands.cpp for manual "fire" test triggers.
void triggerAlarm();

// Called once a "clear" is accepted (serial, or auto-clear when
// LATCH_ALARM is false). `reason` is logged to Serial.
void clearAlarm(const char *reason);

#endif
