#ifndef ALARM_H
#define ALARM_H

void alarmInit();
void handleFireState();

// Local detection: called by dust_sensor.cpp once sustained smoke is
// confirmed, and by serial_commands.cpp / wifi_dashboard.cpp for manual
// "fire" test triggers. Starts a new alarm episode on this node and floods a
// FIRE packet to the mesh (see mesh.cpp).
void triggerAlarm();

// Local clear (serial, dashboard, or auto-clear when LATCH_ALARM is false).
// Floods a CLEAR for every active alarm on the mesh. `reason` is logged.
void clearAlarm(const char *reason);

// Local silence: buzzers off on every node, alarms stay active.
void silenceAlarm();

// Called by mesh.cpp whenever the set of active alarms changes. `active` =
// at least one alarm episode is live (own or remote); `buzzerSilenced` = every
// live episode has been silenced. Drives fireActive / buzzer / alarm LED.
void alarmApply(bool active, bool buzzerSilenced);

#endif
