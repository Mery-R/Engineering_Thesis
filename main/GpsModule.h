#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <time.h>

// Helper structure for returning a complete set of data
struct GpsDataPacket {
    double lat;
    double lon;
    double alt;
    double vel;
    uint32_t satellites;
    double hdop;
    bool valid;
};

// GPS Module (Quectel L80 / PAIR commands)
class GpsModule {
public:
    // Constructor: accepts pin numbers, baudrate and UART number (default 1 for ESP32)
    GpsModule(int rxPin, int txPin, long baudRate = 115200, int uartNr = 1);

    void begin(); // Serial port initialization
    void wake(); // Wake up module (Quectel PAIR commands)
    void sleep(); // Sleep module (Quectel PAIR commands)

    int available(); // Checks if data available in serial buffer
    bool process(); // Main read function - processes incoming data

    bool hasFix(); // Checks if we have a current fix
    GpsDataPacket getData(); // Returns structure with data

    bool isTimeAvailable(); // Checks if time is available
    uint64_t getUnixTime(); // Returns Unix time in ms (synchronized with GPS)

private:
    HardwareSerial _gpsSerial;
    TinyGPSPlus _gps;
    
    int _rxPin;
    int _txPin;
    long _baudRate;
    
    unsigned long _lastFixTime;
    bool _fixAcquired;
    
    const unsigned long GPS_DATA_TIMEOUT_MS = 5000; 
    const bool DEBUG_RAW = false;

    bool validateChecksum(); // Checksum helper
};
