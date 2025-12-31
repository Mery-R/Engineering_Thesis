#pragma once
#include <SD.h>
#include <ArduinoJson.h>
#include <freertos/semphr.h>
#include "SensorData.h"
#include "TimeManager.h"

extern SemaphoreHandle_t sdMutex; // Global variable from main.ino

// SD Card Module for logging data
class SdModule {
public:
    SdModule(int csPin);

    // Integrated initialization and state check
    bool ensureReady(bool force = false);

    // Checks if SD is ready
    bool isReady() const;

    // Archiving
    bool logToArchive(const SensorData* batch, int count);
    String getLatestArchiveFilename();

    // Pending (Offline buffer)
    bool logToPending(const SensorData* batch, int count);
    int readPendingBatch(JsonArray &outArray, int maxItems); // Reads pending data (FIFO)
    bool removeFirstRecords(int count); // Removes 'count' first lines from the pending file

private:
    int _csPin;
    bool _initialized = false;
    unsigned long _lastRetryTime = 0;
    
    String _currentArchiveFilename = "";
    const char* _pendingFilename = "/pending.jsonl";
    const size_t MAX_FILE_SIZE = 5 * 1024 * 1024; // 5MB limit

    String generateArchiveFilename(); // Generates archive filename based on date
    void rotateArchiveFile(); // Rotates (creates new) archive file
    void checkArchiveSizeAndRotate(); // Checks if rotation is needed
};