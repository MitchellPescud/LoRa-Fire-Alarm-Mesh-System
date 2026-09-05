#include <Arduino.h>
#include "alarm.h"
#include "config.h"
#include "state.h"

static unsigned long lastBuzzMs    = 0;
static unsigned long buzzerOffAtMs = 0;
static unsigned long lastFlashMs   = 0;
static bool fireLedState = false;

void alarmInit() {
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_FIRE_LED, OUTPUT);
  digitalWrite(PIN_FIRE_LED, LOW);
}

// ---------------------------------------------------------------------------
// Buzzer chirp every 1s, rapid fire-LED flash, while fireActive is true.
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

  if (now - lastFlashMs >= FIRE_LED_FLASH_MS) {
    lastFlashMs = now;
    fireLedState = !fireLedState;
    digitalWrite(PIN_FIRE_LED, fireLedState ? HIGH : LOW);
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
  lastFlashMs = millis();
  fireLedState = false;
  Serial.println("[ALERT] SMOKE DETECTED - alarm triggered!");
}

void clearAlarm(const char *reason) {
  fireActive = false;
  alarmLatched = false;
  digitalWrite(PIN_FIRE_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  Serial.println(reason);
}
