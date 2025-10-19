#pragma once
#include <TinyGPSPlus.h>

// TinyGPS++ obiekt
extern TinyGPSPlus gps;

// UART dla GPS
extern HardwareSerial GPSSerial;

void gpsInit(long baudrate, int RX, int TX);
bool gpsRead();
