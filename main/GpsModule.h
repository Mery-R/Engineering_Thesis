#ifndef GPS_MODULE_H
#define GPS_MODULE_H

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <time.h>

// Struktura pomocnicza do zwracania kompletu danych
struct GpsDataPacket {
    double lat;
    double lon;
    double elevation;
    double speed;
    uint32_t satellites;
    double hdop;
    bool valid;
};

class GpsModule {
public:
    // Konstruktor: przyjmuje numery pinów, baudrate i numer UART (domyślnie 1 dla ESP32)
    GpsModule(int rxPin, int txPin, long baudRate = 115200, int uartNr = 1);

    // Inicjalizacja portu szeregowego
    void begin();

    // Wybudzanie modułu (Quectel PAIR commands)
    void wake();

    // Usypianie modułu (Quectel PAIR commands)
    void sleep();

    // Główna funkcja odczytu - powinna być wołana w pętli przez określony czas
    // Zwraca true, jeśli odebrano i zdekodowano poprawne zdanie NMEA
    bool process();

    // Sprawdza, czy mamy aktualny FIX (na podstawie valid flag i czasu ostatniego odczytu)
    bool hasFix();

    // Zwraca strukturę z danymi
    GpsDataPacket getData();

    // Zwraca czas uniksowy w ms (zsynchronizowany z GPS)
    uint64_t getUnixTime();

    // Sprawdza czy czas jest dostępny
    bool isTimeAvailable();

    // Loguje status GPS (sukces/błąd) na Serial
    void logStatus(bool gpsOk);

private:
    HardwareSerial _gpsSerial;
    TinyGPSPlus _gps;
    
    int _rxPin;
    int _txPin;
    long _baudRate;
    
    unsigned long _lastFixTime;
    bool _fixAcquired;
    
    // Stałe konfiguracyjne
    const unsigned long GPS_DATA_TIMEOUT_MS = 5000; 
    const bool DEBUG_RAW = false;

    // Helper do sumy kontrolnej (opcjonalny, bo TinyGPS+ sprawdza sumy, 
    // ale potrzebny jeśli chcemy ręcznie weryfikować komunikaty PAIR)
    bool validateChecksum();
};

#endif // GPS_MODULE_H
