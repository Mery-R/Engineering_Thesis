#include "GpsModule.h"
#include <Arduino.h>
#include <time.h>

#define GPS_RAW_DEBUG false
#define GPS_NO_DATA_TIMEOUT 2000  // ms bez danych

static TinyGPSPlus gps;
static HardwareSerial GPSSerial(1);
static unsigned long lastGpsDataTime = 0;

// Store current GPS values as primitives (struct is kept in main only)
static double currentLat = 0.0;
static double currentLon = 0.0;
static double currentElevation = 0.0;
static double currentSpeed = 0.0;
static bool fixAcquired = false;
static unsigned long gpsStartTime = 0;

void gpsInit(long baudrate, int RX, int TX) {
    Serial.println("[GPS] Inicjalizacja portu szeregowego...");
    GPSSerial.begin(baudrate, SERIAL_8N1, RX, TX);
    Serial.printf("[GPS] Baudrate: %ld | RX: %d | TX: %d\n", baudrate, RX, TX);
    lastGpsDataTime = millis();
    gpsStartTime = millis();
}

void gpsSleep() {
    Serial.println("[GPS] Przejście w tryb Standby...");
    GPSSerial.println("$PAIR382,1*2E");
    delay(100);
    GPSSerial.println("$PAIR003*39");
}

void gpsWake() {
    Serial.println("[GPS] Wybudzanie...");
    GPSSerial.println("$PAIR002*38");
    delay(2000);
}

bool gpsRead() {
    bool newFrame = false;

    while (GPSSerial.available() > 0) {
        char c = GPSSerial.read();
        lastGpsDataTime = millis();
        if (GPS_RAW_DEBUG) Serial.write(c);
        
        if (!gps.encode(c)) {
            static unsigned long lastChecksumWarn = 0;
            if (millis() - lastChecksumWarn > 5000) {
                Serial.println("[GPS][WARN] Niepoprawna suma NMEA (ignoruję).");
                lastChecksumWarn = millis();
            }
        } else {
            newFrame = true;
        }
    }

    // Check for GPS data timeout
    if (millis() - lastGpsDataTime > GPS_NO_DATA_TIMEOUT) {
        static unsigned long lastTimeoutWarn = 0;
        if (millis() - lastTimeoutWarn > 10000) {
            Serial.println("[GPS] Brak danych na porcie (timeout).");
            lastTimeoutWarn = millis();
        }
        lastGpsDataTime = millis();
    }

    // Update values if we have valid location and time data
    if (gps.location.isValid() && gps.time.isValid()) {
        currentLat = gps.location.lat();
        currentLon = gps.location.lng();
        currentElevation = gps.altitude.meters();
        currentSpeed = gps.speed.kmph();

        if (!fixAcquired) {
            Serial.printf("[GPS] Fix OK: %.6f, %.6f | Sat: %u | HDOP: %.1f\n",
                          currentLat,
                          currentLon,
                          gps.satellites.value(),
                          gps.hdop.hdop());
            fixAcquired = true;
        }
        return true;
    }

    return false;
}

bool gpsHasFix() {
    // Return true if we have acquired a fix or have been running for 5 minutes
    return fixAcquired || (millis() - gpsStartTime > 5 * 60 * 1000);
}

void getGpsData(double &lat, double &lon, double &elevation, double &speed) {
    lat = currentLat;
    lon = currentLon;
    elevation = currentElevation;
    speed = currentSpeed;
}

uint64_t gpsGetUnixMillis() {
    // Require both date and time
    if (!gps.date.isValid() || !gps.time.isValid()) return 0;

    // Extract components
    int year = gps.date.year();
    if (year < 100) year += 2000; // handle two-digit year
    int month = gps.date.month();
    int day = gps.date.day();
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();

    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = 0;

    // Ensure mktime interprets struct tm as UTC by setting TZ to UTC temporarily
    // Save old TZ if present
    const char* oldtz = getenv("TZ");
    bool hadOldTZ = (oldtz != NULL);
    if (!hadOldTZ) oldtz = "";
    setenv("TZ", "UTC0", 1);
    tzset();

    time_t secs = mktime(&t);

    // Restore TZ
    if (hadOldTZ) setenv("TZ", oldtz, 1);
    else unsetenv("TZ");
    tzset();

    if (secs <= 0) return 0;

    uint64_t ms = (uint64_t)secs * 1000ULL;
    // NMEA provides second precision; milliseconds set to zero
    return ms;
}