#include <Arduino.h>
#include "alarm.h"
#include "config.h"
#include "state.h"
#include "mesh.h"
#include "mesh_test.h"

static unsigned long lastBuzzMs    = 0;
static unsigned long buzzerOffAtMs = 0;
static unsigned long lastFlashMs   = 0;
static bool fireLedState = false;
static bool buzzerSilenced = false;

static void alarmOutputsOff() {
  digitalWrite(PIN_BUZZER, LOW);
  if (PIN_FIRE_LED >= 0) digitalWrite(PIN_FIRE_LED, LOW);
  buzzerOffAtMs = 0;
  fireLedState = false;
}

void alarmInit() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // PIN_FIRE_LED < 0 = no alarm LED fitted/enabled (see config.h: GPIO5 is the
  // radio's PA_CTX pin and must not be driven by us).
  if (PIN_FIRE_LED >= 0) {
    pinMode(PIN_FIRE_LED, OUTPUT);
    digitalWrite(PIN_FIRE_LED, LOW);
  }
}

// ---------------------------------------------------------------------------
// Buzzer chirp every 1s (unless silenced) and rapid alarm-LED flash while
// fireActive is true - whether the alarm is ours or came in over the mesh.
// ---------------------------------------------------------------------------
void handleFireState() {
  if (!fireActive) return;

  unsigned long now = millis();

  if (buzzerSilenced) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerOffAtMs = 0;
  } else {
    if (now - lastBuzzMs >= BUZZ_INTERVAL_MS) {
      lastBuzzMs = now;
      if (!testMute) digitalWrite(PIN_BUZZER, HIGH);   // TEST BUILD: muted by default, `mute off` to hear it
      buzzerOffAtMs = now + BUZZ_ON_MS;
    }
    if (buzzerOffAtMs != 0 && now >= buzzerOffAtMs) {
      digitalWrite(PIN_BUZZER, LOW);
      buzzerOffAtMs = 0;
    }
  }

  if (PIN_FIRE_LED >= 0 && now - lastFlashMs >= FIRE_LED_FLASH_MS) {
    lastFlashMs = now;
    fireLedState = !fireLedState;
    digitalWrite(PIN_FIRE_LED, fireLedState ? HIGH : LOW);
  }
}

void alarmApply(bool active, bool silenced) {
  buzzerSilenced = silenced;
  if (active && !fireActive) {
    fireActive = true;
    alarmLatched = true;
    lastBuzzMs = millis() - BUZZ_INTERVAL_MS;  // fire the first chirp immediately
    buzzerOffAtMs = 0;
    lastFlashMs = millis();
    fireLedState = false;
    Serial.println("[ALERT] ALARM ACTIVE");
  } else if (!active && fireActive) {
    fireActive = false;
    alarmLatched = false;
    alarmOutputsOff();
    Serial.println("[INFO] Alarm ended");
  }
  if (silenced) digitalWrite(PIN_BUZZER, LOW);
}

void triggerAlarm() {
  if (meshLocalEpisodeActive()) {
    Serial.println("[INFO] Alarm already active, ignoring duplicate trigger");
    return;
  }
  Serial.println("[ALERT] SMOKE DETECTED - alarm triggered!");
  meshStartLocalEpisode();
}

void clearAlarm(const char *reason) {
  Serial.println(reason);
  meshLocalClearAll();
}

void silenceAlarm() {
  if (!fireActive) {
    Serial.println("[INFO] No active alarm to silence");
    return;
  }
  meshLocalSilence();
}
