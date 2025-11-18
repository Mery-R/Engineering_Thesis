#include "SdModule.h"
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

SdModule::SdModule(uint8_t csPin)
    : _csPin(csPin), _filename("/data.json") {}

bool SdModule::begin() {
    if (!SPI.begin()) {
        Serial.println("[SD] SPI init failed");
        return false;
    }

    if (!SD.begin(_csPin)) {
        Serial.println("[SD] SD init failed");
        return false;
    }

    if (!SD.exists(_filename)) {
        File f = SD.open(_filename, FILE_WRITE);
        if (!f) {
            Serial.println("[SD] Could not create file");
            return false;
        }
        f.print("[]");
        f.close();
        Serial.println("[SD] Created empty JSON file");
    }

    Serial.println("[SD] Ready");
    return true;
}

bool SdModule::appendRecords(JsonArray &records) {
    // If no records to append, nothing to do
    if (records.size() == 0) return true;

    File f = SD.open(_filename, FILE_READ);
    if (!f) {
        Serial.println("[SD] Cannot open file for reading");
        return false;
    }

    String content;
    while (f.available()) content += (char)f.read();
    f.close();

    StaticJsonDocument<8192> doc;
    if (deserializeJson(doc, content)) {
        Serial.println("[SD] JSON parse error");
        return false;
    }

    JsonArray jsonArr = doc.as<JsonArray>();

    // copy each incoming JsonObject into the SD array
    for (JsonObject src : records) {
        JsonObject dst = jsonArr.createNestedObject();
        dst["timestamp"] = src["timestamp"] | 0ULL;
        dst["lat"] = src["lat"] | 0.0;
        dst["lon"] = src["lon"] | 0.0;
        dst["elevation"] = src["elevation"] | 0.0;
        dst["speed"] = src["speed"] | 0.0;
        dst["temp"] = src["temp"] | 0.0f;
        dst["time_source"] = src["time_source"] | 0;
        dst["last_gps_fix_timestamp"] = src["last_gps_fix_timestamp"] | 0ULL;
        dst["last_temp_read_timestamp"] = src["last_temp_read_timestamp"] | 0ULL;
        dst["error_code"] = src["error_code"] | 0;
        dst["tb_sent"] = src["tb_sent"] | false;
    }

    // overwrite file with updated doc
    SD.remove(_filename);
    f = SD.open(_filename, FILE_WRITE);
    if (!f) {
        Serial.println("[SD] Cannot open file for writing");
        return false;
    }

    serializeJson(doc, f);
    f.close();

    Serial.printf("[SD] Added %d records\n", records.size());
    return true;
}

bool SdModule::clear() {
    SD.remove(_filename);
    File f = SD.open(_filename, FILE_WRITE);
    if (!f) return false;
    f.print("[]");
    f.close();
    Serial.println("[SD] Cleared");
    return true;
}

// ----------------------------
// Pobierz batch rekordów JSON (maxItems) do wysyłki
int SdModule::readBatch(JsonArray &outArray, int maxItems, bool onlyNotSent) {
    File f = SD.open(_filename, FILE_READ);
    if (!f) return 0;

    String content;
    while (f.available()) content += (char)f.read();
    f.close();

    StaticJsonDocument<8192> doc;
    if (deserializeJson(doc, content)) return 0;

    JsonArray arr = doc.as<JsonArray>();
    int count = 0;

    for (JsonObject obj : arr) {
        bool sent = obj["tb_sent"] | false;
        if (!onlyNotSent || !sent) {
            outArray.add(obj);
            count++;
            if (count >= maxItems) break;
        }
    }

    return count;
}

// Oznacz rekordy jako wysłane po sukcesie
void SdModule::markBatchAsSent(JsonArray &sentRecords) {
    File f = SD.open(_filename, FILE_READ);
    if (!f) return;

    String content;
    while (f.available()) content += (char)f.read();
    f.close();

    StaticJsonDocument<8192> doc;
    if (deserializeJson(doc, content)) return;

    JsonArray arr = doc.as<JsonArray>();

    for (JsonObject obj : arr) {
        uint64_t timestamp = obj["timestamp"] | 0ULL;
        for (JsonVariant s : sentRecords) {
            if (timestamp != 0 && timestamp == (uint64_t)(s["timestamp"] | 0ULL)) {
                obj["tb_sent"] = true;
                break;
            }
        }
    }

    // overwrite file with updated doc
    SD.remove(_filename);
    f = SD.open(_filename, FILE_WRITE);
    if (!f) return;

    serializeJson(doc, f);
    f.close();
}
