#pragma once
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include "TimeManager.h"

/**
 * GPS Module Interface
 * 
 * This module provides a simple interface to interact with a GPS receiver
 * connected via UART. The main sensor data structure (SensorData) is intentionally
 * NOT defined here, per project design decision. It's defined in main.ino and used
 * only in the main translation unit.
 * 
 * Thread-safety: These functions are called from TaskGPS and should not be accessed
 * from other tasks without proper synchronization.
 */

// Initialize GPS serial communication
void gpsInit(long baudrate, int RX, int TX);

// Wake GPS from sleep mode
void gpsWake();

// Put GPS into sleep/standby mode
void gpsSleep();

// Read and process incoming GPS data (call frequently in a loop)
bool gpsRead();

// Check if GPS has a valid fix
bool gpsHasFix();

// Get the last known GPS coordinates and speed
void getGpsData(double &lat, double &lon, double &elevation, double &speed);

// Returns GPS-derived Unix timestamp in milliseconds (UTC). If GPS date/time
// is not valid this returns 0.
uint64_t gpsGetUnixMillis();

// Returns true if GPS provides a valid date/time right now
bool gpsTimeAvailable();


