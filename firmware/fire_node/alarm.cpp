#include <Arduino.h>
#include "alarm.h"
#include "config.h"
#include "state.h"

static unsigned long lastBuzzMs    = 0;
static unsigned long buzzerOffAtMs = 0;

void alarmInit() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

// ---------------------------------------------------------------------------
// Buzzer chirp every 1s while fireActive is true. No fire LED on this board
// revision - "light" functionality isn't wired.
// ---------------------------------------------------------------------------
void handleFireState() {
  if (!fireActive) return;

  unsigned long now = millis();

  if (now - lastBuzzMs >= BUZZ_INTERVAL_MS) {
    lastBuzzMs = now;
    digitalWrite(PIN_BUZZER, HIGH);
    buzzerOffAtMs = now + BUZZ_ON_MS;
  }
  if (buzzerOffAtMs != 0 && now >= buzzerOffAtMs) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerOffAtMs = 0;
  }
}

void triggerAlarm() {
  if (fireActive) {
    Serial.println("[INFO] Alarm already active, ignoring duplicate trigger");
    return;
  }
  fireActive = true;
  alarmLatched = true;
  lastBuzzMs = millis() - BUZZ_INTERVAL_MS;  // fire the first chirp immediately
  buzzerOffAtMs = 0;
  Serial.println("[ALERT] SMOKE DETECTED - alarm triggered!");
}

void clearAlarm(const char *reason) {
  fireActive = false;
  alarmLatched = false;
  digitalWrite(PIN_BUZZER, LOW);
  Serial.println(reason);
}
