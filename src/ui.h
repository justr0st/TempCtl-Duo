#pragma once
#include <stdint.h>
void uiInit();
void uiLoop();
void uiSetBrightness(uint8_t b);
// colour palettes of the touch UI (names separated by '\n', index = Settings::themePalette)
const char* uiPaletteNames();
int uiPaletteCount();
