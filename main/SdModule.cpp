#include "SdModule.h"

SdModule::SdModule(int csPin) : _csPin(csPin) {}

bool SdModule::begin() {
    if (_initialized) {
        SD.end(); 
        _initialized = false;
    }

    if (!SD.begin(_csPin)) {
        Serial.println("[SD] Initialization failed!");
        _initialized = false;
        return false;
    }
    Serial.println("[SD] Initialized.");
    _initialized = true;
    
    // Ensure pending file exists
    if (!SD.exists(_pendingFilename)) {
        File p = SD.open(_pendingFilename, FILE_WRITE);
        if (p) {
            p.close();
            Serial.println("[SD] Pending file created.");
        } else {
            Serial.println("[SD] Failed to create pending file!");
        }
    } else {
        Serial.println("[SD] Pending file ready.");
    }
    
    // We do NOT rotate archive here anymore. 
    // We wait until the first write to decide (reuse or new).
    
    return true;
}

String SdModule::generateArchiveFilename() {
    if (TimeManager::isSynchronized()) {
        time_t now = TimeManager::getTimestampMs() / 1000;
        // Set timezone to Poland (CET-1CEST,M3.5.0,M10.5.0/3)
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        
        // Sanity check: ensure year is reasonable (e.g. >= 2024)
        if (timeinfo.tm_year + 1900 < 2024) {
             return "";
        }

        char buf[64];
        // Format: /LOG_YYYYMMDD_HHMMSS.jsonl
        snprintf(buf, sizeof(buf), "/LOG_%04d%02d%02d_%02d%02d%02d.jsonl",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return String(buf);
    } else {
        // If time not synced, we do NOT return a filename yet
        return "";
    }
}

void SdModule::rotateArchiveFile() {
    String newName = generateArchiveFilename();
    if (newName.length() > 0) {
        _currentArchiveFilename = newName;
        Serial.println("[SD] New archive file: " + _currentArchiveFilename);
    } else {
        Serial.println("[SD] Cannot rotate archive: Time not valid yet.");
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
        Serial.println("[SD] File size limit reached. Rotating.");
        rotateArchiveFile();
    }
}

bool SdModule::logToArchive(const SensorData* batch, int count) {
    // If we don't have a filename yet, try to find one or create one
    if (_currentArchiveFilename.length() == 0) {
        // 1. Try to find the latest existing log file
        String latest = getLatestArchiveFilename();
        if (latest.length() > 0) {
            // Check size
            File f = SD.open(latest, FILE_READ);
            if (f) {
                size_t sz = f.size();
                f.close();
                if (sz < MAX_FILE_SIZE) {
                    _currentArchiveFilename = latest;
                    Serial.println("[SD] Reusing existing archive: " + _currentArchiveFilename);
                } else {
                    // Too big, need new one
                    rotateArchiveFile();
                }
            } else {
                // Should not happen if getLatest returned it, but safety
                rotateArchiveFile();
            }
        } else {
            // No existing files, create new
            rotateArchiveFile();
        }
    }

    // If still empty (e.g. no time sync), we cannot save to archive
    if (_currentArchiveFilename.length() == 0) {
        Serial.println("[SD] No archive filename available (Time not synced?). Data NOT saved to archive.");
        return false;
    }

    checkArchiveSizeAndRotate();

    File file = SD.open(_currentArchiveFilename, FILE_APPEND);
    if (!file) {
        // Try rotating once if open failed (maybe invalid name?)
        rotateArchiveFile();
        file = SD.open(_currentArchiveFilename, FILE_APPEND);
        if (!file) {
            Serial.println("[SD] Failed to open archive file: " + _currentArchiveFilename);
            return false;
        }
    }

    for (int i = 0; i < count; ++i) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        sensorDataToJson(batch[i], obj);
        
        if (serializeJson(doc, file) == 0) {
            Serial.println("[SD] Failed to write record to archive");
        }
        file.println(); 
    }
    
    Serial.printf("[SD] Saved %d records to Archive: %s\n", count, _currentArchiveFilename.c_str());

    file.close();
    return true;
}

bool SdModule::logJsonToArchive(const JsonArray& array) {
    // Reuse the same logic for filename finding/creation
    if (_currentArchiveFilename.length() == 0) {
        String latest = getLatestArchiveFilename();
        if (latest.length() > 0) {
            File f = SD.open(latest, FILE_READ);
            if (f) {
                size_t sz = f.size();
                f.close();
                if (sz < MAX_FILE_SIZE) {
                    _currentArchiveFilename = latest;
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
        Serial.println("[SD] No archive filename available. Data NOT saved to archive.");
        return false;
    }

    checkArchiveSizeAndRotate();

    File file = SD.open(_currentArchiveFilename, FILE_APPEND);
    if (!file) {
        rotateArchiveFile();
        file = SD.open(_currentArchiveFilename, FILE_APPEND);
        if (!file) {
            Serial.println("[SD] Failed to open archive file: " + _currentArchiveFilename);
            return false;
        }
    }

    for (JsonVariant v : array) {
        if (serializeJson(v, file) == 0) {
            Serial.println("[SD] Failed to write JSON record to archive");
        }
        file.println(); 
    }
    
    Serial.println("[SD] Saved JSON batch to Archive: " + _currentArchiveFilename);
    file.close();
    return true;
}

bool SdModule::logToPending(const SensorData* batch, int count) {
    File file = SD.open(_pendingFilename, FILE_APPEND);
    if (!file) {
        Serial.println("[SD] Failed to open pending file");
        return false;
    }

    for (int i = 0; i < count; ++i) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        sensorDataToJson(batch[i], obj);
        
        // Ensure tb_sent is false for pending data
        obj["tb_sent"] = false;
        if (serializeJson(doc, file) == 0) {
            Serial.println("[SD] Failed to write record to pending");
        }
        file.println(); 
    }
    Serial.printf("[SD] Saved %d records to Pending\n", count);
    return true;
}

int SdModule::readPendingBatch(JsonArray &outArray, int maxItems, size_t &offset) {
    if (!SD.exists(_pendingFilename)) return 0;

    File file = SD.open(_pendingFilename, FILE_READ);
    if (!file) return 0;

    if (offset > 0) {
        if (!file.seek(offset)) {
            file.close();
            return 0;
        }
    }

    int count = 0;
    int processed = 0;
    while (file.available() && count < maxItems) {
        // Yield every 10 lines to prevent WDT trigger
        if (++processed % 10 == 0) vTaskDelay(1);

        String line = file.readStringUntil('\n');
        line.trim();
        
        // Update offset to point to the start of the next line
        offset = file.position();

        if (line.length() == 0) continue;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        if (error) {
            Serial.print("[SD] JSON parse error in pending: ");
            Serial.println(error.c_str());
            continue;
        }

        outArray.add(doc);
        count++;
    }
    file.close();
    return count;
}

bool SdModule::clearPending() {
    if (SD.exists(_pendingFilename)) {
        Serial.println("[SD] Clearing pending file.");
        return SD.remove(_pendingFilename);
    }
    return true;
}

bool SdModule::isReady() const {
    return _initialized;
}

String SdModule::getLatestArchiveFilename() {
    File root = SD.open("/");
    if (!root) return "";
    if (!root.isDirectory()) {
        root.close();
        return "";
    }

    String latestFile = "";

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            // Check if it matches /LOG_ pattern (simple check)
            // Note: SD library might return name without leading /
            if (name.startsWith("LOG_") || name.startsWith("/LOG_")) {
                if (name.endsWith(".jsonl")) {
                    // Lexicographical comparison works for YYYYMMDD_HHMMSS format
                    // Ensure we compare just the names, handle potential leading slash
                    String cleanName = name;
                    if (!cleanName.startsWith("/")) cleanName = "/" + cleanName;
                    
                    if (latestFile == "" || cleanName > latestFile) {
                        latestFile = cleanName;
                    }
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();
    return latestFile;
}

bool SdModule::checkAndRemount() {
    if (SD.cardType() == CARD_NONE) {
        Serial.println("[SD] Card not detected. Attempting remount...");
        SD.end();
        _initialized = false; 

        if (SD.begin(_csPin)) {
            Serial.println("[SD] Remount successful.");
            _initialized = true;
            // Re-check pending file existence just in case
            if (!SD.exists(_pendingFilename)) {
                 File p = SD.open(_pendingFilename, FILE_WRITE);
                 if(p) p.close();
            }
            return true;
        } else {
            Serial.println("[SD] Remount failed.");
            return false;
        }
    }
    // If we are here, cardType is not NONE, so card is physically detected.
    // However, we might want to ensure _initialized is true if it wasn't.
    if (!_initialized) _initialized = true; 
    return true;
}
