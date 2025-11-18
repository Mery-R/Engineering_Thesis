#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "TimeConfig.h"      // jeśli używasz funkcji getTimestamp(), isTimeSynced()
#include "GpsModule.h"       // nadal potrzebne do interfejsu GPS
#include <ArduinoJson.h>


class SdModule {
public:
    SdModule(uint8_t csPin);

    bool begin();

    // Dodaje nowe rekordy do pliku (przekaż JsonArray przygotowany w main)
    bool appendRecords(JsonArray &records);

    // Odczytuje batch rekordów (maks. maxItems) do JsonArray
    // onlyNotSent: jeśli true - zwraca tylko rekordy z tb_sent == false
    int readBatch(JsonArray &outArray, int maxItems, bool onlyNotSent = true);

    // Oznacza rekordy jako wysłane (przekazany JsonArray zawiera rekordy wysłane)
    void markBatchAsSent(JsonArray &sentRecords);

    bool clear();

private:
    uint8_t _csPin;
    String _filename;
};
