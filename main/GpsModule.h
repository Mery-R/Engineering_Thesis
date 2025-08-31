#pragma once
#include <TinyGPSPlus.h>

extern TinyGPSPlus gps;

void gpsInit(long baudrate, int RX, int TX);
bool gpsRead();
