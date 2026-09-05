#ifndef DISPLAY_H
#define DISPLAY_H

#include <HT_SSD1306Wire.h>

// Heltec's board library already declares a global `display` object as soon
// as HT_SSD1306Wire.h is used (via LoRaWan_APP.cpp, compiled in as part of
// the Heltec board package) - reuse it instead of declaring a second
// SSD1306Wire instance. A second instance is what left the OLED dark in an
// earlier version of this project.
extern SSD1306Wire display;

void displayInit();
void handleDisplay();

#endif
