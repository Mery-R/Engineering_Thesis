/*
 * Time Configuration Module (Optional NTP Setup)
 * 
 * This file provides guidance for implementing NTP time synchronization
 * for the ESP32 to enable accurate timestamping without GPS.
 * 
 * Add this to main.ino setup() if you want NTP synchronization:
 */

#ifndef TIME_CONFIG_H
#define TIME_CONFIG_H

#include <time.h>
#include <sntp.h>

// NTP Server Configuration (static to avoid multiple definition)
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.cloudflare.com";
static const char* NTP_SERVER_3 = "time.google.com";

// Timezone Configuration (Poland = UTC+1, or UTC+2 during summer)
static const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0";

/**
 * Initialize NTP synchronization
 * Call this from setup() after WiFi is connected
 */
static inline void initializeNTP() {
    Serial.println("[TIME] Configuring SNTP...");
    
    // Configure SNTP
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    
    // Set timezone
    setenv("TZ", TIMEZONE, 1);
    tzset();
    
    Serial.println("[TIME] Waiting for NTP time synchronization...");
    
    // Wait for time to be set
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 24 * 3600 && attempts < 100) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        attempts++;
    }
    
    Serial.println();
    if (now > 24 * 3600) {
        Serial.printf("[TIME] NTP Synchronized: %s\n", ctime(&now));
    } else {
        Serial.println("[TIME] NTP synchronization timeout - using local time");
    }
}

/**
 * Get current timestamp with fallback.
 * Returns Unix timestamp in milliseconds if NTP is synced,
 * otherwise returns millis() for relative timing.
 */
inline uint64_t getTimestamp() {
    time_t now = time(nullptr);
    if (now > 24 * 3600) {
        return (uint64_t)now * 1000ULL;  // Valid NTP time in ms
    } else {
        return (uint64_t)millis();  // Fallback to millis
    }
}

/**
 * Check if time is synchronized via NTP
 */
inline bool isTimeSynced() {
    time_t now = time(nullptr);
    return (now > 24 * 3600);  // Any time after Jan 1, 1970 + 1 day
}

#endif // TIME_CONFIG_H

/*
 * USAGE IN main.ino:
 * 
 * In setup() after WiFi connection:
 *   WiFi.begin(WIFI_SSID, WIFI_PASS);
 *   // ... wait for WiFi connection ...
 *   if (WiFi.status() == WL_CONNECTED) {
 *       initializeNTP();  // <-- Add this line
 *   }
 * 
 * Replace time() calls with:
 *   uint64_t now = getTimestamp();
 * 
 * Check sync status with:
 *   if (isTimeSynced()) { ... }
 */
