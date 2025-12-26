#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "SensorData.h"
#include <ArduinoJson.h>
#include "TimeManager.h"

class SdModule {
public:
    SdModule(int csPin);

    bool begin();

    // Logs a batch to the current archive file (rotates if needed)
    bool logToArchive(const SensorData* batch, int count);

    // Logs a batch to the pending file (for failed sends)
    bool logToPending(const SensorData* batch, int count);

    // Logs a JSON array to the archive (used when moving data from pending to archive)
    bool logJsonToArchive(const JsonArray& array);

    // Reads a batch from the pending file starting at offset
    int readPendingBatch(JsonArray &outArray, int maxItems, size_t &offset);

    // Clears the pending file (after successful retry)
    bool clearPending();

    bool isReady() const;

    // Checks if SD card is present and attempts to remount if not
    bool checkAndRemount();

private:
    int _csPin;
    String _currentArchiveFilename;
    const char* _pendingFilename = "/pending.jsonl";
    const size_t MAX_FILE_SIZE = 500 * 1024; // 500KB
    bool _initialized = false;
    unsigned long _lastRetryTime = 0;

    void rotateArchiveFile();
    String generateArchiveFilename();
    void checkArchiveSizeAndRotate();
    String getLatestArchiveFilename();
};
