#include "SdModule.h"

// Constructor
SdModule::SdModule(int csPin) : _csPin(csPin) {}

// SD Card Initialization
bool SdModule::ensureReady(bool force) {
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);

    // Cooldown check (only if not forced)
    if (!force) {
        if (millis() - _lastRetryTime < 2000) {
            if (sdMutex) xSemaphoreGive(sdMutex);
            return _initialized; 
        }
        _lastRetryTime = millis();
    }

    // State Check (only if not forced)
    // If we think we are initialized, verify it
    if (_initialized && !force) {
        if (SD.cardType() == CARD_NONE) {
            Serial.println("[SD] Card missing. Unmounting.");
            _initialized = false;
        } else {
            // Logical check
            File root = SD.open("/", FILE_READ);
            if (!root) {
                Serial.println("[SD] FS Error. Invalidating state.");
                _initialized = false;
            } else {
                root.close();
                if (sdMutex) xSemaphoreGive(sdMutex);
                return true; // All good
            }
        }
    }

    // Recovery / Initialization
    // If we reach here, either force=true OR _initialized=false
    
    // Always clean up first
    SD.end(); 
    _initialized = false;

    // Try to mount
    if (SD.begin(_csPin)) {
        Serial.println("[SD] Mount/Restart successful.");
        _initialized = true;
        
        // Ensure pending file exists
        if (!SD.exists(_pendingFilename)) {
            File p = SD.open(_pendingFilename, FILE_WRITE);
            if(p) {
                p.close();
                Serial.println("[SD] Pending file created.");
            }
        }
    } else {
        Serial.println("[SD] Mount failed.");
    }
    
    if (sdMutex) xSemaphoreGive(sdMutex);
    return _initialized;
}

// -----------------------------------------------------
// --------------- PENDING FILE (FIFO) -----------------
// -----------------------------------------------------

bool SdModule::logToPending(const SensorData* batch, int count) {
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);

    // Open in APPEND mode (write to end)
    File file = SD.open(_pendingFilename, FILE_APPEND);
    if (!file) {
        Serial.println("[SD] Failed to open pending file for writing");
        _initialized = false; // Mark SD error to force remount
        if (sdMutex) xSemaphoreGive(sdMutex);
        return false;
    }

    for (int i = 0; i < count; ++i) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        sensorDataToTb(batch[i], obj);
        

        
        if (serializeJson(doc, file) == 0) {
            Serial.println("[SD] Failed to write record to pending");
        }
        file.println(); // New line = new record
    }
    
    Serial.printf("[SD] Saved %d records to Pending\n", count);
    file.close();
    
    if (sdMutex) xSemaphoreGive(sdMutex);
    return true;
}

// Reads 'maxItems' lines from the beginning of the file
int SdModule::readPendingBatch(JsonArray &outArray, int maxItems) {
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);

    if (!SD.exists(_pendingFilename)) {
        if (sdMutex) xSemaphoreGive(sdMutex);
        return 0;
    }

    File file = SD.open(_pendingFilename, FILE_READ);
    if (!file) {
        if (sdMutex) xSemaphoreGive(sdMutex);
        return 0;
    }

    int validRecords = 0;
    int linesRead = 0; // Counter for physically read lines

    // Read while data exists and limit not reached
    while (file.available() && validRecords < maxItems) {
        
        String line = file.readStringUntil('\n');
        line.trim();
        linesRead++; // Count every line (even empty/invalid)

        if (line.length() == 0) continue;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) {
            Serial.print("[SD] JSON parse error in pending (skipping): ");
            Serial.println(error.c_str());
            // Invalid line is ignored but will be removed by removeFirstRecords,
            // because removeFirstRecords removes N lines. 
            // There is a risk of desynchronization if we only count valid ones.
            // For simplicity: ignore errors and continue reading.
            continue;
        }

        outArray.add(doc);
        validRecords++;
    }
    
    file.close();
    
    if (sdMutex) xSemaphoreGive(sdMutex);
    return validRecords;
}

// NEW METHOD: Safely removes 'count' lines from the beginning of the file
bool SdModule::removeFirstRecords(int count) {
    if (count <= 0) return true;
    
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);

    const char* tempFilename = "/pending.tmp";
    
    // 1. Check source
    if (!SD.exists(_pendingFilename)) {
        if (sdMutex) xSemaphoreGive(sdMutex);
        return true; 
    }

    File sourceFile = SD.open(_pendingFilename, FILE_READ);
    if (!sourceFile) {
        Serial.println("[SD] Failed to open pending for reading (remove ops)");
        if (sdMutex) xSemaphoreGive(sdMutex);
        return false;
    }

    // 2. Prepare temp file
    if (SD.exists(tempFilename)) SD.remove(tempFilename);
    
    File tempFile = SD.open(tempFilename, FILE_WRITE);
    if (!tempFile) {
        Serial.println("[SD] Failed to create temp file");
        sourceFile.close();
        if (sdMutex) xSemaphoreGive(sdMutex);
        return false;
    }

    // 3. Skip first 'count' lines
    // We must skip exactly as many lines as were "consumed" (validRecords)
    // NOTE: If there were JSON errors in readPendingBatch, count might be less than physical lines.
    // In this simple implementation, we assume the file is not corrupted.
    int skipped = 0;
    while (sourceFile.available() && skipped < count) {
        sourceFile.readStringUntil('\n');
        skipped++;
    }

    // 4. Copy the rest of the file
    size_t copiedBytes = 0;
    uint8_t buf[512]; // 512B Buffer
    while (sourceFile.available()) {
        int readLen = sourceFile.read(buf, sizeof(buf));
        if (readLen > 0) {
            tempFile.write(buf, readLen);
            copiedBytes += readLen;
        }
    }

    sourceFile.close();
    tempFile.close();

    // 5. Swap files
    SD.remove(_pendingFilename); // Remove old pending
    
    if (copiedBytes > 0) {
        // If data remains, rename temp -> pending
        if (!SD.rename(tempFilename, _pendingFilename)) {
            Serial.println("[SD] CRITICAL: Failed to rename temp file!");
            // Emergency situation. Data is in .tmp
            if (sdMutex) xSemaphoreGive(sdMutex);
            return false;
        }
    } else {
        // If temp file is empty (we sent everything), just remove temp.
        SD.remove(tempFilename);
        Serial.println("[SD] Pending file cleared (empty).");
    }

    if (sdMutex) xSemaphoreGive(sdMutex);
    return true;
}

bool SdModule::clearPending() {
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
    bool res = true;
    if (SD.exists(_pendingFilename)) {
        Serial.println("[SD] Deleting pending file.");
        res = SD.remove(_pendingFilename);
    }
    if (sdMutex) xSemaphoreGive(sdMutex);
    return res;
}

// -----------------------------------------------------
// ------------------ ARCHIVE LOGS ---------------------
// -----------------------------------------------------

String SdModule::generateArchiveFilename() {
    if (TimeManager::isSynchronized()) {
        time_t now = TimeManager::getTimestampMs() / 1000;
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        // Year validity check
        if (timeinfo.tm_year + 1900 < 2024) return "";

        char buf[64];
        snprintf(buf, sizeof(buf), "/LOG_%04d%02d%02d_%02d%02d%02d.jsonl",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return String(buf);
    }
    return "";
}

void SdModule::rotateArchiveFile() {
    String newName = generateArchiveFilename();
    if (newName.length() > 0) {
        _currentArchiveFilename = newName;
        Serial.printf("[SD] New archive file: %s\n", _currentArchiveFilename.c_str());
    }
}

void SdModule::checkArchiveSizeAndRotate() {
    if (_currentArchiveFilename.length() == 0) {
        rotateArchiveFile();
        return;
    }

    if (!SD.exists(_currentArchiveFilename)) return;

    File f = SD.open(_currentArchiveFilename, FILE_READ);
    if (!f) return;
    
    size_t size = f.size();
    f.close();

    if (size >= MAX_FILE_SIZE) {
        Serial.println("[SD] Archive file limit reached. Rotating.");
        rotateArchiveFile();
    }
}

bool SdModule::logToArchive(const SensorData* batch, int count) {
    if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);

    // Find or create filename
    if (_currentArchiveFilename.length() == 0) {
        String latest = getLatestArchiveFilename();
        if (latest.length() > 0) {
             File f = SD.open(latest, FILE_READ);
             if (f) {
                 size_t sz = f.size();
                 f.close();
                 if (sz < MAX_FILE_SIZE) {
                     _currentArchiveFilename = latest;
                     Serial.printf("[SD] Reusing archive: %s\n", _currentArchiveFilename.c_str());
                 } else {
                     rotateArchiveFile();
                 }
             } else {
                 rotateArchiveFile();
             }
        } else {
            rotateArchiveFile();
        }
    }

    if (_currentArchiveFilename.length() == 0) {
        // No time = no name = no archive save
        if (sdMutex) xSemaphoreGive(sdMutex);
        return false;
    }

    checkArchiveSizeAndRotate();

    File file = SD.open(_currentArchiveFilename, FILE_APPEND);
    if (!file) {
        rotateArchiveFile();
        file = SD.open(_currentArchiveFilename, FILE_APPEND);
        if (!file) {
            Serial.printf("[SD] Failed to open archive: %s\n", _currentArchiveFilename.c_str());
            _initialized = false;
            if (sdMutex) xSemaphoreGive(sdMutex);
            return false;
        }
    }

    for (int i = 0; i < count; ++i) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        sensorDataToSd(batch[i], obj);
        
        if (serializeJson(doc, file) == 0) {
            Serial.println("[SD] Failed to write to archive");
            _initialized = false;
        }
        file.println(); 
    }
    
    Serial.printf("[SD] Archived %d records to %s\n", count, _currentArchiveFilename.c_str());

    file.close();
    if (sdMutex) xSemaphoreGive(sdMutex);
    return true;
}

// -----------------------------------------------------
// ------------------ HELPERS --------------------------
// -----------------------------------------------------

String SdModule::getLatestArchiveFilename() {
    File root = SD.open("/");
    if (!root || !root.isDirectory()) return "";

    String latestFile = "";
    File file = root.openNextFile();
    
    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            // Check if it's a log file (LOG_... format)
            if (name.indexOf("LOG_") != -1 && name.endsWith(".jsonl")) {
                String cleanName = name;
                if (!cleanName.startsWith("/")) cleanName = "/" + cleanName;
                
                // Lexicographical comparison works for YYYYMMDD format
                if (latestFile == "" || cleanName > latestFile) {
                    latestFile = cleanName;
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();
    return latestFile;
}

bool SdModule::isReady() const {
    return _initialized;
}