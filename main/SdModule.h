#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "TimeManager.h"      // jeśli używasz funkcji getTimestamp(), isTimeSynced()
#include "GpsModule.h"       // nadal potrzebne do interfejsu GPS
#include <ArduinoJson.h>


class SdModule {
public:
    SdModule(uint8_t csPin);

    bool begin();

    // Dodaje nowe rekordy do pliku (przekaż JsonArray przygotowany w main)
    // Records are stored as newline-delimited JSON arrays (each line = one batch array)
    // Each call appends one JSON array (one line) containing the records.
    bool appendRecords(JsonArray &records);

    // Odczytuje batch rekordów (maks. maxItems) do JsonArray
    // onlyNotSent: jeśli true - zwraca tylko rekordy z tb_sent == false
    int readBatch(JsonArray &outArray, int maxItems, bool onlyNotSent = true);

    // Oznacza rekordy jako wysłane (przekazany JsonArray zawiera rekordy wysłane)
    void markBatchAsSent(JsonArray &sentRecords);

    // (Single append method removed - use appendRecords(JsonArray&) to write a batch)

    bool clear();

private:
    uint8_t _csPin;
    String _filename;
    
    // Helper to populate a single TB-style object item in array
    void appendItemToArray(JsonObject &item, JsonObject &src);
};
