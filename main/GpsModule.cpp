#include "GpsModule.h"

GpsModule::GpsModule(int rxPin, int txPin, long baudRate, int uartNr)
    : _gpsSerial(uartNr), _rxPin(rxPin), _txPin(txPin), _baudRate(baudRate), 
      _lastFixTime(0), _fixAcquired(false) {
}

// -----------------------------------------------------
// --------------- Public Methods ----------------------
// -----------------------------------------------------

void GpsModule::begin() {
    Serial.print("[GPS] Initializing GpsModule object ---> ");
    _gpsSerial.begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
    Serial.printf("Config: Baud=%ld, RX=%d, TX=%d ---> ", _baudRate, _rxPin, _txPin);
    _lastFixTime = millis();
    Serial.println("Initialization finished");
}

void GpsModule::wake() {
    Serial.println("[GPS] WAKE");
    // According to Quectel documentation: $PAIR002*38
    _gpsSerial.println("$PAIR002*38");
    // Short delay for startup
    delay(200); 
}

void GpsModule::sleep() {
    Serial.println("[GPS] SLEEP");
    // 1. Lock System Sleep - $PAIR382,1*2E
    //    Dokumentacja str. 42: "CM4 will entry Standby if application not working."
    _gpsSerial.println("$PAIR382,1*2E");
    delay(100);
    
    // 2. Power Off GNSS system - $PAIR003*39
    //    Dokumentacja str. 22: "Powers off the GNSS system... CM4 will be set to the Standby mode."
    _gpsSerial.println("$PAIR003*39");
}

int GpsModule::available() {
    return _gpsSerial.available();
}

bool GpsModule::process() {
    bool encoded = false;

    while (_gpsSerial.available() > 0) {
            char c = _gpsSerial.read();
            if (DEBUG_RAW) Serial.write(c);

            if (_gps.encode(c)) {
                encoded = true;
                
                // If we have valid location data, update timestamp
                if (_gps.location.isValid()) {
                    _lastFixTime = millis();
                    if (!_fixAcquired) {
                        Serial.println("[GPS] First FIX after wake/startup!");
                        _fixAcquired = true;
                    }
                }
            }
        }
        
        // Check data timeout (if module transmits anything)
        if (millis() - _lastFixTime > (GPS_DATA_TIMEOUT_MS * 4) && _fixAcquired) {
             // Warning only if we had a fix, and now total silence on the line
             // Serial.println("[GPS][WARN] Long silence on RX port.");
        }
    return encoded;
}

bool GpsModule::hasFix() {
    // We consider FIX valid if library says isValid AND data is fresh (e.g. < 5 sec)
    // TinyGPSPlus location.age() returns age in ms since last update
    if (!_gps.location.isValid()) return false;
    return (_gps.location.age() < 5000);
}

GpsDataPacket GpsModule::getData() {
    GpsDataPacket packet = {0.0, 0.0, 0.0, 0.0, 0, 0.0, false};
    
    if (hasFix()) {
        packet.lat = _gps.location.lat();
        packet.lon = _gps.location.lng();
        packet.alt = _gps.altitude.meters();
        packet.vel = _gps.speed.kmph();
        packet.satellites = _gps.satellites.value();
        packet.hdop = _gps.hdop.hdop();
        packet.valid = true;
    } 
    
    return packet;
}

bool GpsModule::isTimeAvailable() {
    return _gps.date.isValid() && _gps.time.isValid();
}

uint64_t GpsModule::getUnixTime() {
    if (!isTimeAvailable()) return 0;

    // Get time components
    int year = _gps.date.year();
    // Handle year format (library usually returns full year, but just in case)
    if (year < 100) year += 2000; 
    
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon  = _gps.date.month() - 1;
    t.tm_mday = _gps.date.day();
    t.tm_hour = _gps.time.hour();
    t.tm_min  = _gps.time.minute();
    t.tm_sec  = _gps.time.second();
    t.tm_isdst = 0;

    // Set timezone to UTC for mktime
    const char* oldtz = getenv("TZ");
    setenv("TZ", "UTC0", 1);
    tzset();

    time_t secs = mktime(&t);

    // Restore timezone (for system order)
    if (oldtz) setenv("TZ", oldtz, 1);
    else unsetenv("TZ");
    tzset();

    if (secs <= 0) return 0;

    return (uint64_t)secs * 1000ULL;
}
