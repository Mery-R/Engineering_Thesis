#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <time.h>


// Źródło czasu
enum TimeSource {
    TIME_LOCAL = 0,
    TIME_WIFI  = 1,
    TIME_GPS   = 2
};

class TimeManager {
public:
    static void begin(int PPS_PIN = -1);
    
    static void IRAM_ATTR handlePPS();

    static void syncTime(uint64_t unixMs);
    static uint64_t getTimestampMs();
    static bool isSynchronized();

    static void updateFromGps(uint64_t gpsUnixMs);
    static void periodicCheck();

    static TimeSource getTimeSource();

private:
    static volatile uint64_t baseUnixMs;
    static volatile uint32_t baseMillis;
    static volatile bool timeValid;

    static TimeSource currentSource;

    static bool ntpEnabled;
    static uint32_t lastPeriodicCheck;

    static uint64_t getNtpTimeMs();
    static const uint64_t MIN_VALID_UNIX_MS;
    
    static volatile uint32_t lastPpsMillis;
};

#endif
