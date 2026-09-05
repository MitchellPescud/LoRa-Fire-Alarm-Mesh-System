#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

// Starts the I2C bus (shared with the OLED) and the BMP280. Must be called
// after Vext has been powered on, and before displayInit().
void tempSensorInit();

void handleTemp();

#endif
