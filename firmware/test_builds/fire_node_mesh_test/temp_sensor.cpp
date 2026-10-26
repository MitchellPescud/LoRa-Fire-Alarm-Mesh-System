#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include "temp_sensor.h"
#include "config.h"
#include "state.h"

// Only this file talks to the sensor directly - other modules just read
// bmpOk/lastTemp via state.h, so the Adafruit_BMP280 object stays local here.
static Adafruit_BMP280 bmp;
static unsigned long lastTempMs = 0;

void tempSensorInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  bmpOk = bmp.begin(0x76) || bmp.begin(0x77);
  if (bmpOk) {
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                     Adafruit_BMP280::SAMPLING_X2,
                     Adafruit_BMP280::SAMPLING_X16,
                     Adafruit_BMP280::FILTER_X16,
                     Adafruit_BMP280::STANDBY_MS_500);
    Serial.println("[BOOT] BMP280 detected.");
  } else {
    Serial.println("[BOOT] WARNING: BMP280 not detected - temp will show N/A.");
  }
}

void handleTemp() {
  unsigned long now = millis();
  if (now - lastTempMs < TEMP_POLL_MS) return;
  lastTempMs = now;

  if (bmpOk) {
    lastTemp = bmp.readTemperature();
  }
}
