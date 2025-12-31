#pragma once
#include <Arduino.h>
#include <time.h>

// Time source
enum TimeSource {
    TIME_LOCAL = 0,
    TIME_WIFI  = 1,
    TIME_GPS   = 2
};

// Time Manager (Static Class)
class TimeManager {
public:
    static void begin(int PPS_PIN = -1); // Initialize time manager
    
    // Interrupt Service Routine for PPS signal
    static void IRAM_ATTR handlePPS(); 

    static void syncTime(uint64_t unixMs); // Sync time manually
    static uint64_t getTimestampMs(); // Get current timestamp in ms
    static bool isSynchronized(); // Check if time is synchronized

    static void updateFromGps(uint64_t gpsUnixMs); // Update time from GPS

    static TimeSource getTimeSource(); // Get current time source

private:
    static volatile uint64_t baseUnixMs;
    static volatile uint32_t baseMillis;
    static volatile bool timeValid;
    static volatile uint32_t lastPpsMillis;

    static TimeSource currentSource;

    static bool ntpEnabled;
    static uint32_t lastPeriodicCheck;

    static const uint64_t MIN_VALID_UNIX_MS;

    static uint64_t getNtpTimeMs(); // Get time from NTP
};
