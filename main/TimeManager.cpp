#include "TimeManager.h"
#include <time.h>
#include <sntp.h>
#include <WiFi.h>

// --- static variables ---
volatile uint64_t TimeManager::baseUnixMs = 0;
volatile uint32_t TimeManager::baseMillis = 0;
volatile bool TimeManager::timeValid = false;
volatile uint32_t TimeManager::lastPpsMillis = 0;
TimeSource TimeManager::currentSource = TIME_LOCAL;

const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.google.com";
const char* ntpServer3 = "time.cloudflare.com";
bool TimeManager::ntpEnabled = false;
uint32_t TimeManager::lastPeriodicCheck = 0;

// Minimum timestamp (ms) considered valid - smaller values are ignored
const uint64_t TimeManager::MIN_VALID_UNIX_MS = 1763651027000ULL;

// -----------------------------------------------------
// --------------- Public Methods ----------------------
// -----------------------------------------------------

// --- begin ---
void TimeManager::begin(int PPS_PIN) {
    if (PPS_PIN >= 0) {
        pinMode(PPS_PIN, INPUT);
        attachInterrupt(digitalPinToInterrupt(PPS_PIN), handlePPS, RISING);
    }
    
    // Auto-enable NTP
    configTime(0, 0, ntpServer1, ntpServer2, ntpServer3);
    ntpEnabled = true;
}

void IRAM_ATTR TimeManager::handlePPS() {
    uint32_t now = millis();
    lastPpsMillis = now;

    // Optional: If we are already valid/synced, we can increment the second counter here
    // for smoother transitions between GPS updates.
    if (timeValid && baseUnixMs >= MIN_VALID_UNIX_MS) {
        // If we are roughly 1 second from last baseMillis, increment
        uint32_t delta = now - baseMillis;
        if (delta > 900 && delta < 1100) {
             baseUnixMs += 1000;
             baseMillis = now;
        }
    }
}
// --- Update from GPS ---
void TimeManager::updateFromGps(uint64_t gpsUnixMs) {
    if (gpsUnixMs == 0) return;
    if (gpsUnixMs < MIN_VALID_UNIX_MS) {
        Serial.println("[TimeManager] updateFromGps: gps time below MIN_VALID_UNIX_MS, ignoring");
        return;
    }

    // If PPS was received recently (within last 900ms), align to it
    // Usually GPS data comes 100-500ms after PPS.
    if (millis() - lastPpsMillis < 900) {
         baseUnixMs = gpsUnixMs;
         baseMillis = lastPpsMillis; 
    } else {
         baseUnixMs = gpsUnixMs;
         baseMillis = millis();
    }
    
    timeValid = true;
    currentSource = TIME_GPS;
}

// --- Synchronization ---
void TimeManager::syncTime(uint64_t unixMs) {
    if (unixMs >= MIN_VALID_UNIX_MS) {
        baseUnixMs = unixMs;
        baseMillis = millis();
        timeValid = true;
        currentSource = TIME_WIFI;
    } else {
        Serial.println("[TimeManager] syncTime: rejected too-small unixMs");
    }
}






// --- getTimestampMs ---
uint64_t TimeManager::getTimestampMs() {
    // 1. GPS or previously synchronized time
    if (timeValid && baseUnixMs >= MIN_VALID_UNIX_MS) {
        uint32_t delta = millis() - baseMillis;
        return baseUnixMs + delta;
    }

    // 2. NTP if available
    uint64_t ntpMs = getNtpTimeMs();
    if (ntpMs > 0) {
        // If it's the first time, save as base
        if (!timeValid) {
            syncTime(ntpMs);
        }
        currentSource = TIME_WIFI;
        return ntpMs;
    }

    // 3. No source -> local time
    currentSource = TIME_LOCAL;
    return millis();
}

// --- getTimeSource ---
TimeSource TimeManager::getTimeSource() {
    return currentSource;
}




// --- isSynchronized ---
bool TimeManager::isSynchronized() {
    if (timeValid && baseUnixMs >= MIN_VALID_UNIX_MS) return true;
    return (time(nullptr) > 100000);
}

// -----------------------------------------------------
// --------------- Private Methods ---------------------
// -----------------------------------------------------

// --- getNtpTimeMs ---
uint64_t TimeManager::getNtpTimeMs() {
    if (!ntpEnabled) return 0;
    time_t t = time(nullptr);
    if (t > 100000) return ((uint64_t)t) * 1000ULL;
    return 0;
}