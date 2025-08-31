#include "GpsModule.h"
#include <HardwareSerial.h>

TinyGPSPlus gps;
HardwareSerial GPSSerial(1);

void gpsInit(long baudrate, int RX, int TX) {
    GPSSerial.begin(baudrate, SERIAL_8N1, RX, TX); // RX=16, TX=17
}

// Ustaw na true, aby debugować surowe dane GPS na Serial
#define GPS_RAW_DEBUG false

bool gpsRead() {
    bool gotData = false;
    while (GPSSerial.available() > 0) {
        char c = GPSSerial.read();
        if (GPS_RAW_DEBUG) Serial.write(c); // debug surowych danych NMEA
        if (gps.encode(c)) {
            gotData = true;
        }
    }
    return gotData && gps.location.isValid();
}
