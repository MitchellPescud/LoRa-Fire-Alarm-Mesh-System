#ifndef DISPLAY_H
#define DISPLAY_H

#include <HT_SSD1306Wire.h>

// Heltec's board library already declares the global `display` object once
// HT_SSD1306Wire.h is used - reuse it, don't declare a second instance.
extern SSD1306Wire display;

void displayInit();
void handleDisplay();

#endif
