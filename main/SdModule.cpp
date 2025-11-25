#include "SdModule.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <ArduinoJson.h>
#include <vector>

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
        // create empty JSON array file
        f.print("[]");
        f.close();
        Serial.println("[SD] Created empty JSON array file");
    }

    Serial.println("[SD] Ready");
    return true;
}

bool SdModule::softClose() {
    if (_file) {
        _file.flush();
        _file.close();
    }

    SD.end();
    delay(50);
    return true;
}

bool SdModule::appendRecords(JsonArray &records) {
    // If no records to append, nothing to do
    if (records.size() == 0) return true;
    // Build a temporary JSON array containing TB-style items, then append it as one line
    StaticJsonDocument<8192> batchDoc; // per-batch doc
    JsonArray arr = batchDoc.to<JsonArray>();

    for (JsonObject src : records) {
        JsonObject item = arr.createNestedObject();

        // compute ts in ms
        uint64_t ts_raw = (uint64_t)(src["timestamp"] | 0ULL);
        auto to_ms = [](uint64_t v) -> uint64_t {
            if (v >= 1000000000000ULL) return v;
            if (v >= 1000000000ULL) return v * 1000ULL;
            return 0ULL;
        };
        uint64_t ts_ms = to_ms(ts_raw);
        if (ts_ms == 0ULL) {
            uint64_t alt = (uint64_t)(src["last_temp_read_timestamp"] | 0ULL);
            ts_ms = to_ms(alt);
        }
        if (ts_ms == 0ULL) ts_ms = (uint64_t) millis();

        item["ts"] = ts_ms;
        JsonObject values = item.createNestedObject("values");
        values["lat"] = (double)(src["lat"] | 0.0);
        values["lon"] = (double)(src["lon"] | 0.0);
        values["elevation"] = (double)(src["elevation"] | 0.0);
        values["speed"] = (double)(src["speed"] | 0.0);
        values["temp"] = (double)(src["temp"] | 0.0);
        values["time_source"] = (int)(src["time_source"] | 0);

        uint64_t lg = (uint64_t)(src["last_gps_fix_timestamp"] | 0ULL);
        values["last_gps_fix_timestamp"] = (lg >= 1000000000000ULL ? lg : (lg >= 1000000000ULL ? lg * 1000ULL : 0ULL));

        uint64_t lt = (uint64_t)(src["last_temp_read_timestamp"] | 0ULL);
        values["last_temp_read_timestamp"] = (lt >= 1000000000000ULL ? lt : (lt >= 1000000000ULL ? lt * 1000ULL : 0ULL));

        values["error_code"] = (int)(src["error_code"] | 0);
        values["tb_sent"] = (bool)(src["tb_sent"] | false);
    }

    // Append serialized array as single line
    File fw = SD.open(_filename, FILE_APPEND);
    if (!fw) {
        Serial.println("[SD] Cannot open file for appending");
        return false;
    }
    fw.seek(fw.size());
    serializeJson(arr, fw);
    fw.print('\n');
    fw.close();

    Serial.printf("[SD] Appended %d records (batch line)\n", records.size());
    return true;
}

// single-record append removed; use appendRecords(JsonArray&) to write batches

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

    int count = 0;
    String line;
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\r') continue;
        if (c != '\n') {
            line += c;
            continue;
        }

        if (line.length() == 0) { line = ""; continue; }

        StaticJsonDocument<8192> doc;
        DeserializationError err = deserializeJson(doc, line);
        line = "";
        if (err) continue;
        if (!doc.is<JsonArray>()) continue;

        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject root : arr) {
            bool sent = false;
            if (root.containsKey("values") && root["values"].is<JsonObject>()) {
                JsonObject vals = root["values"].as<JsonObject>();
                sent = vals["tb_sent"] | false;
            }

            if (!onlyNotSent || !sent) {
                JsonObject dst = outArray.createNestedObject();
                dst["ts"] = root["ts"] | 0ULL;
                JsonObject dvals = dst.createNestedObject("values");
                if (root.containsKey("values") && root["values"].is<JsonObject>()) {
                    JsonObject srcvals = root["values"].as<JsonObject>();
                    dvals["lat"] = srcvals["lat"] | 0.0;
                    dvals["lon"] = srcvals["lon"] | 0.0;
                    dvals["elevation"] = srcvals["elevation"] | 0.0;
                    dvals["speed"] = srcvals["speed"] | 0.0;
                    dvals["temp"] = srcvals["temp"] | 0.0;
                    dvals["time_source"] = srcvals["time_source"] | 0;
                    dvals["last_gps_fix_timestamp"] = srcvals["last_gps_fix_timestamp"] | 0ULL;
                    dvals["last_temp_read_timestamp"] = srcvals["last_temp_read_timestamp"] | 0ULL;
                    dvals["error_code"] = srcvals["error_code"] | 0;
                    dvals["tb_sent"] = srcvals["tb_sent"] | false;
                }
                count++;
                if (count >= maxItems) break;
            }
        }
        if (count >= maxItems) break;
    }

    // handle last line without trailing newline
    if (line.length() > 0 && count < maxItems) {
        StaticJsonDocument<8192> doc;
        if (!deserializeJson(doc, line) && doc.is<JsonArray>()) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject root : arr) {
                bool sent = false;
                if (root.containsKey("values") && root["values"].is<JsonObject>()) {
                    JsonObject vals = root["values"].as<JsonObject>();
                    sent = vals["tb_sent"] | false;
                }
                if (!onlyNotSent || !sent) {
                    JsonObject dst = outArray.createNestedObject();
                    dst["ts"] = root["ts"] | 0ULL;
                    JsonObject dvals = dst.createNestedObject("values");
                    if (root.containsKey("values") && root["values"].is<JsonObject>()) {
                        JsonObject srcvals = root["values"].as<JsonObject>();
                        dvals["lat"] = srcvals["lat"] | 0.0;
                        dvals["lon"] = srcvals["lon"] | 0.0;
                        dvals["elevation"] = srcvals["elevation"] | 0.0;
                        dvals["speed"] = srcvals["speed"] | 0.0;
                        dvals["temp"] = srcvals["temp"] | 0.0;
                        dvals["time_source"] = srcvals["time_source"] | 0;
                        dvals["last_gps_fix_timestamp"] = srcvals["last_gps_fix_timestamp"] | 0ULL;
                        dvals["last_temp_read_timestamp"] = srcvals["last_temp_read_timestamp"] | 0ULL;
                        dvals["error_code"] = srcvals["error_code"] | 0;
                        dvals["tb_sent"] = srcvals["tb_sent"] | false;
                    }
                    count++;
                    if (count >= maxItems) {}
                }
            }
        }
    }

    f.close();
    return count;
}

// Oznacz rekordy jako wysłane po sukcesie
void SdModule::markBatchAsSent(JsonArray &sentRecords) {
    // Read file line-by-line (each line = JSON array), update items with matching ts, then rewrite file
    File f = SD.open(_filename, FILE_READ);
    if (!f) return;

    // collect timestamps to mark
    std::vector<uint64_t> toMark;
    for (JsonVariant s : sentRecords) {
        if (s.is<JsonObject>()) {
            uint64_t ts = s["ts"] | 0ULL;
            if (ts != 0) toMark.push_back(ts);
        }
    }

    std::vector<String> updatedLines;
    String line;
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\r') continue;
        if (c != '\n') { line += c; continue; }

        if (line.length() == 0) { line = ""; continue; }

        StaticJsonDocument<8192> doc;
        if (!deserializeJson(doc, line) && doc.is<JsonArray>()) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject root : arr) {
                uint64_t ts = root["ts"] | 0ULL;
                for (uint64_t m : toMark) {
                    if (m != 0 && m == ts) {
                        if (root.containsKey("values") && root["values"].is<JsonObject>()) {
                            root["values"]["tb_sent"] = true;
                        }
                        break;
                    }
                }
            }
            String out;
            serializeJson(arr, out);
            updatedLines.push_back(out);
        } else {
            // if parsing failed, keep original line to avoid data loss
            updatedLines.push_back(line);
        }
        line = "";
    }

    // last line
    if (line.length() > 0) {
        StaticJsonDocument<8192> doc;
        if (!deserializeJson(doc, line) && doc.is<JsonArray>()) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject root : arr) {
                uint64_t ts = root["ts"] | 0ULL;
                for (uint64_t m : toMark) {
                    if (m != 0 && m == ts) {
                        if (root.containsKey("values") && root["values"].is<JsonObject>()) {
                            root["values"]["tb_sent"] = true;
                        }
                        break;
                    }
                }
            }
            String out;
            serializeJson(arr, out);
            updatedLines.push_back(out);
        } else {
            updatedLines.push_back(line);
        }
    }

    f.close();

    // rewrite file with updated lines
    SD.remove(_filename);
    File fw = SD.open(_filename, FILE_WRITE);
    if (!fw) return;
    for (size_t i = 0; i < updatedLines.size(); ++i) {
        fw.print(updatedLines[i]);
        fw.print('\n');
    }
    fw.close();
}
