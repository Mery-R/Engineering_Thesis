#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include "GpsModule.h"

class TimeManager {
public:
    // Inicjalizacja z opcjonalnym pinem PPS
    static void begin(int PPS_PIN = -1);

    // Synchronizacja czasu z GPS/NTP
    static void syncTime(uint64_t unixMs);

    // Enable NTP backup (jeśli GPS niedostępny)
    static void enableNtpBackup(const char* s1, const char* s2 = nullptr, const char* s3 = nullptr);

    // Pobierz aktualny timestamp w ms
    static uint64_t getTimestampMs();

    // Sprawdź, czy czas jest zsynchronizowany
    static bool isSynchronized();

    // Update z GPS (wywołuj w TaskGPS)
    static void updateFromGps();

    // Co 5 minut możesz wywołać check, żeby skorygować ESP clock
    static void periodicCheck();

private:
    static volatile uint64_t baseUnixMs;   // czas synchronizowany (ms od 1970)
    static volatile uint32_t baseMillis;   // millis() w momencie synchronizacji
    static volatile bool timeValid;

    // NTP backup
    static const char* ntpServer1;
    static const char* ntpServer2;
    static const char* ntpServer3;
    static bool ntpEnabled;
    static uint32_t lastPeriodicCheck;

    static uint64_t getNtpTimeMs();
    // Minimal valid unix ms timestamp (to reject garbage GPS times)
    static const uint64_t MIN_VALID_UNIX_MS;
};

#endif // TIME_MANAGER_H
