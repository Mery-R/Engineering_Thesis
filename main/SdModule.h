#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "SensorData.h"
#include <ArduinoJson.h>

class SdModule {
public:
    SdModule(int csPin);

    bool begin();

    // Appends a batch of records in JSONLines format
    bool appendBatch(const SensorData* batch, int count);

    // Checks file size and rotates if necessary
    void checkFileSizeAndRotate();

    // Reads a batch of records from the file (parsing JSONLines)
    // NOTE: This needs to be implemented to support ThingsBoardClient
    int readBatch(JsonArray &outArray, int maxItems, bool onlyNotSent = true);

    // Marks records as sent (in JSONLines, this might require rewriting the file or appending a "sent" log)
    // For now, we might need a placeholder or a different strategy for "mark as sent" with append-only files.
    void markBatchAsSent(JsonArray &sentRecords);

    bool clear();

private:
    int _csPin;
    const char* _filename = "/data.jsonl";
    const size_t MAX_FILE_SIZE = 1024 * 1024; // 1MB

    void rotateFile();
};
