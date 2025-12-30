#ifndef SDMODULE_H
#define SDMODULE_H

#include <SD.h>
#include <ArduinoJson.h>
#include "SensorData.h"
#include "TimeManager.h"
#include <freertos/semphr.h>

extern SemaphoreHandle_t sdMutex; // Global variable from main.ino

class SdModule {
public:
    SdModule(int csPin);
    
    // Integrated initialization and state check
    bool ensureReady(bool force = false);
    
    // Archiving
    bool logToArchive(const SensorData* batch, int count);

    // Pending (Offline buffer)
    bool logToPending(const SensorData* batch, int count);
    
    // CHANGE: Removed 'offset' parameter, as we always read from the beginning (FIFO)
    int readPendingBatch(JsonArray &outArray, int maxItems);
    
    // NEW FUNCTION: Removes 'count' first lines from the pending file
    bool removeFirstRecords(int count);
    
    bool clearPending(); // Deletes the entire file

    bool isReady() const;

private:
    int _csPin;
    bool _initialized = false;
    unsigned long _lastRetryTime = 0;
    
    String _currentArchiveFilename = "";
    const char* _pendingFilename = "/pending.jsonl";
    const size_t MAX_FILE_SIZE = 5 * 1024 * 1024; // 5MB limit

    String generateArchiveFilename();
    void rotateArchiveFile();
    void checkArchiveSizeAndRotate();
    String getLatestArchiveFilename();
};

#endif