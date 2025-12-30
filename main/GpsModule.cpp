#include "GpsModule.h"

GpsModule::GpsModule(int rxPin, int txPin, long baudRate, int uartNr)
    : _gpsSerial(uartNr), _rxPin(rxPin), _txPin(txPin), _baudRate(baudRate), 
      _lastFixTime(0), _fixAcquired(false) {
}

void GpsModule::begin() {
    Serial.print("[GPS] Inicjalizacja obiektu GpsModule ---> ");
    _gpsSerial.begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
    Serial.printf("Config: Baud=%ld, RX=%d, TX=%d ---> ", _baudRate, _rxPin, _txPin);
    _lastFixTime = millis();
    Serial.println("Inicjalizacja zakończona");
}

void GpsModule::wake() {
    Serial.println("[GPS] WAKE");
    // Zgodnie z dokumentacją Quectel: $PAIR002*38
    _gpsSerial.println("$PAIR002*38");
    // Krótkie opóźnienie na rozruch
    delay(200); 
}

void GpsModule::sleep() {
    Serial.println("[GPS] SLEEP");
    // 1. Zablokowanie trybu uśpienia (Lock System Sleep) - $PAIR382,1*2E
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
                
                // Jeśli mamy ważne dane lokalizacyjne, aktualizujemy timestamp
                if (_gps.location.isValid()) {
                    _lastFixTime = millis();
                    if (!_fixAcquired) {
                        Serial.println("[GPS] Pierwszy FIX po wybudzeniu/starcie!");
                        _fixAcquired = true;
                    }
                }
            }
        }
        
        // Sprawdzenie timeoutu danych (czy moduł w ogóle coś nadaje)
        if (millis() - _lastFixTime > (GPS_DATA_TIMEOUT_MS * 4) && _fixAcquired) {
             // Ostrzeżenie tylko jeśli mieliśmy fixa, a teraz cisza totalna na linii
             // Serial.println("[GPS][WARN] Długa cisza na porcie RX.");
        }
    return encoded;
}

bool GpsModule::hasFix() {
    // Uznajemy FIX za ważny, jeśli biblioteka mówi isValid ORAZ dane są świeże (np. < 5 sek)
    // TinyGPSPlus location.age() zwraca wiek w ms od ostatniej aktualizacji
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

    // Pobranie komponentów czasu
    int year = _gps.date.year();
    // Obsługa formatu roku (biblioteka zwykle zwraca pełny rok, ale dla pewności)
    if (year < 100) year += 2000; 
    
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon  = _gps.date.month() - 1;
    t.tm_mday = _gps.date.day();
    t.tm_hour = _gps.time.hour();
    t.tm_min  = _gps.time.minute();
    t.tm_sec  = _gps.time.second();
    t.tm_isdst = 0;

    // Ustawienie strefy czasowej na UTC dla mktime
    const char* oldtz = getenv("TZ");
    setenv("TZ", "UTC0", 1);
    tzset();

    time_t secs = mktime(&t);

    // Przywrócenie strefy (dla porządku w reszcie systemu)
    if (oldtz) setenv("TZ", oldtz, 1);
    else unsetenv("TZ");
    tzset();

    if (secs <= 0) return 0;

    return (uint64_t)secs * 1000ULL;
}

void GpsModule::logStatus(bool gpsOk) {
    if (gpsOk && hasFix()) {
        Serial.printf("[GPS] Fix acquired! Lat: %f, Lon: %f\n", _gps.location.lat(), _gps.location.lng());
    } else {
        if (!gpsOk) {
            Serial.println("[GPS] Error: Module not responding");
        } else {
            Serial.println("[GPS] No fix acquired");
        }
    }
}
