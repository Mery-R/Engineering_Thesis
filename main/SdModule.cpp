#include "SdModule.h"

SdModule::SdModule(int csPin) : _csPin(csPin) {}

bool SdModule::begin() {
    if (!SD.begin(_csPin)) {
        Serial.println("[SD] Initialization failed!");
        return false;
    }
    Serial.println("[SD] Initialized.");
    
    // Check size on startup
    checkFileSizeAndRotate();
    
    return true;
}

void SdModule::checkFileSizeAndRotate() {
    if (!SD.exists(_filename)) return;

    File f = SD.open(_filename, FILE_READ);
    if (!f) return;
    
    size_t size = f.size();
    f.close();

    if (size >= MAX_FILE_SIZE) {
        rotateFile();
    }
}

void SdModule::rotateFile() {
    Serial.println("[SD] Rotating file...");
    
    String backupName = "/data_backup_";
    backupName += String(millis());
    backupName += ".jsonl";

    if (SD.rename(_filename, backupName.c_str())) {
        Serial.printf("[SD] Rotated to %s\n", backupName.c_str());
    } else {
        Serial.println("[SD] Rotation failed!");
    }
}

bool SdModule::appendBatch(const SensorData* batch, int count) {
    checkFileSizeAndRotate();

    File file = SD.open(_filename, FILE_APPEND);
    if (!file) {
        Serial.println("[SD] Failed to open file for appending");
        return false;
    }

    for (int i = 0; i < count; ++i) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        sensorDataToJson(batch[i], obj);
        
        if (serializeJson(doc, file) == 0) {
            Serial.println("[SD] Failed to write record");
        }
        file.println(); 
    }

    file.close();
    return true;
}


bool SdModule::clear() {
    if (SD.exists(_filename)) {
        return SD.remove(_filename);
    }
    return true;
}

// Reads a batch of records from the file (parsing JSONLines)
int SdModule::readBatch(JsonArray &outArray, int maxItems, bool onlyNotSent) {
    if (!SD.exists(_filename)) return 0;

    File file = SD.open(_filename, FILE_READ);
    if (!file) return 0;

    int count = 0;
    int processed = 0;
    while (file.available() && count < maxItems) {
        // Yield every 10 lines to prevent WDT trigger
        if (++processed % 10 == 0) vTaskDelay(1);

        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        if (error) {
            Serial.print("[SD] JSON parse error: ");
            Serial.println(error.c_str());
            continue;
        }

        bool sent = doc["tb_sent"] | false;
        if (onlyNotSent && sent) continue;

        outArray.add(doc);
        count++;
    }
    file.close();
    return count;
}

// Marks records as sent (rewrites the file)
void SdModule::markBatchAsSent(JsonArray &sentRecords) {
    if (!SD.exists(_filename)) return;

    String tempFilename = "/temp_data.jsonl";
    File originalFile = SD.open(_filename, FILE_READ);
    File tempFile = SD.open(tempFilename, FILE_WRITE);

    if (!originalFile || !tempFile) {
        Serial.println("[SD] Failed to open files for marking sent");
        if (originalFile) originalFile.close();
        if (tempFile) tempFile.close();
        return;
    }

    int processed = 0;
    while (originalFile.available()) {
        // Yield every 10 lines to prevent WDT trigger
        if (++processed % 10 == 0) vTaskDelay(1);

        String line = originalFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        if (error) {
            // If we can't parse it, keep it as is? Or drop? 
            // Better to keep it to avoid data loss, but we can't check timestamp.
            // Let's just write the raw line back.
            tempFile.println(line);
            continue;
        }

        // Check if this record is in the sentRecords batch
        uint64_t ts = doc["timestamp"];
        bool found = false;
        for (JsonVariant v : sentRecords) {
            if ((uint64_t)v["timestamp"] == ts) {
                found = true;
                break;
            }
        }

        if (found) {
            doc["tb_sent"] = true;
        }

        if (serializeJson(doc, tempFile) == 0) {
            Serial.println("[SD] Error writing to temp file");
        }
        tempFile.println();
    }

    originalFile.close();
    tempFile.close();

    SD.remove(_filename);
    SD.rename(tempFilename, _filename);
    Serial.println("[SD] Marked records as sent (file rewritten)");
}
