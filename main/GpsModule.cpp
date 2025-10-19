#include "GpsModule.h"
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

// --- Obiekty globalne ---
TinyGPSPlus gps;
HardwareSerial GPSSerial(1);  // UART1

#define GPS_RAW_DEBUG false
#define GPS_NO_DATA_TIMEOUT 2000  // ms bez danych, po których pojawia się komunikat

unsigned long lastGpsDataTime = 0;

void gpsInit(long baudrate, int RX, int TX) {
    Serial.println("[GPS] Inicjalizacja portu szeregowego...");
    GPSSerial.begin(baudrate, SERIAL_8N1, RX, TX);
    Serial.printf("[GPS] Baudrate: %ld | RX: %d | TX: %d\n", baudrate, RX, TX);
    Serial.println("[GPS] Port szeregowy zainicjalizowany.");
    lastGpsDataTime = millis();
}

bool gpsRead() {
    bool newFrame = false;

    while (GPSSerial.available() > 0) {
        char c = GPSSerial.read();
        lastGpsDataTime = millis(); // reset licznika, bo są dane
        if (GPS_RAW_DEBUG) Serial.write(c);
        if (gps.encode(c)) newFrame = true;
    }

    // jeśli przez dłuższy czas nie przyszły dane
    if (millis() - lastGpsDataTime > GPS_NO_DATA_TIMEOUT) {
        Serial.println("[GPS] Brak danych na porcie.");
        lastGpsDataTime = millis(); // żeby nie spamowało
    }

    if (!newFrame) return false;

    if (gps.failedChecksum()) {
        Serial.println("[GPS][BŁĄD] Niepoprawna suma kontrolna NMEA.");
        return false;
    }

    if (!gps.location.isValid())
        Serial.println("[GPS][BŁĄD] Niepoprawna pozycja GPS.");
    if (!gps.date.isValid() || !gps.time.isValid())
        Serial.println("[GPS][INFO] Czekam na synchronizację czasu...");
    if (gps.satellites.value() == 0)
        Serial.println("[GPS][BŁĄD] Brak satelitów.");

    if (gps.location.isValid() && gps.time.isValid()) {
        Serial.printf("[GPS] Fix OK: %.6f, %.6f | Sat: %u | HDOP: %.1f\n",
                      gps.location.lat(),
                      gps.location.lng(),
                      gps.satellites.value(),
                      gps.hdop.hdop());
        return true;
    }

    return false;
}
