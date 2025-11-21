#include "TimeManager.h"
#include <time.h>
#include <sntp.h>
#include <WiFi.h>

// --- zmienne statyczne ---
volatile uint64_t TimeManager::baseUnixMs = 0;
volatile uint32_t TimeManager::baseMillis = 0;
volatile bool TimeManager::timeValid = false;

const char* TimeManager::ntpServer1 = nullptr;
const char* TimeManager::ntpServer2 = nullptr;
const char* TimeManager::ntpServer3 = nullptr;
bool TimeManager::ntpEnabled = false;
uint32_t TimeManager::lastPeriodicCheck = 0;

// Minimalny timestamp (ms) uważany za sensowny - mniejsze wartości ignorujemy
const uint64_t TimeManager::MIN_VALID_UNIX_MS = 1763651027000ULL;

// --- begin ---
void TimeManager::begin(int PPS_PIN) {
    if (PPS_PIN >= 0) pinMode(PPS_PIN, INPUT);
    // tu możesz attachInterrupt(PPS_PIN, ...) jeśli chcesz PPS
}

// --- enable NTP backup ---
void TimeManager::enableNtpBackup(const char* s1, const char* s2, const char* s3) {
    ntpServer1 = s1;
    ntpServer2 = s2;
    ntpServer3 = s3;
    ntpEnabled = true;
    configTime(0, 0, s1, s2, s3);
}

// --- synchronizacja ---
void TimeManager::syncTime(uint64_t unixMs) {
    if (unixMs >= MIN_VALID_UNIX_MS) {
        baseUnixMs = unixMs;
        baseMillis = millis();
        timeValid = true;
    } else {
        Serial.println("[TimeManager] syncTime: rejected too-small unixMs");
    }
}

// --- aktualizacja z GPS ---
void TimeManager::updateFromGps() {
    uint64_t gpsUnixMs = gpsGetUnixMillis(); // wywołanie funkcji z GPSModule
    if (gpsUnixMs == 0) return;             // brak valid time
    if (gpsUnixMs < MIN_VALID_UNIX_MS) {
        // ignore obviously wrong GPS time
        Serial.println("[TimeManager] updateFromGps: gps time below MIN_VALID_UNIX_MS, ignoring");
        return;
    }
    baseUnixMs = gpsUnixMs;
    baseMillis = millis();
    timeValid = true;
}


// --- getTimestampMs ---
uint64_t TimeManager::getTimestampMs() {
    if (timeValid && baseUnixMs >= MIN_VALID_UNIX_MS) {
        uint32_t delta = millis() - baseMillis;
        return baseUnixMs + delta;
    }

    uint64_t ntpMs = getNtpTimeMs();
    if (ntpMs > 0) return ntpMs;

    return millis();
}

// --- isSynchronized ---
bool TimeManager::isSynchronized() {
    if (timeValid && baseUnixMs >= MIN_VALID_UNIX_MS) return true;
    return (time(nullptr) > 100000);
}

// --- getNtpTimeMs ---
uint64_t TimeManager::getNtpTimeMs() {
    if (!ntpEnabled || WiFi.status() != WL_CONNECTED) return 0;
    time_t t = time(nullptr);
    if (t > 100000) return ((uint64_t)t) * 1000ULL;
    return 0;
}

// --- periodic check ---
void TimeManager::periodicCheck() {
    if (millis() - lastPeriodicCheck < 5 * 60 * 1000) return; // 5 minut
    lastPeriodicCheck = millis();

    uint64_t current = getTimestampMs();
    uint64_t ntpMs = getNtpTimeMs();
    if (ntpMs > 0) {
        int64_t diff = (int64_t)(ntpMs - current);
        if (abs(diff) > 2000) { // jeśli różnica > 2s
            Serial.printf("[TimeManager] Correcting ESP clock by %lld ms\n", diff);
            syncTime(current + diff);
        }
    }
}
