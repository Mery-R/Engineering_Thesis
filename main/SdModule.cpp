#include "SdModule.h"

SdModule::SdModule(uint8_t csPin) : _csPin(csPin), _filename("/data.json") {}

bool SdModule::begin() {
    if (!SD.begin(_csPin)) {
        Serial.println("[SD] Initialization failed!");
        return false;
    }
    Serial.println("[SD] SD ready.");
    return true;
}

bool SdModule::writeJson(double lat, double lon, double elev, double speed, double temp) {
    File file = SD.open(_filename, FILE_APPEND);
    if (!file) {
        Serial.println("[SD] Error opening file!");
        return false;
    }

    StaticJsonDocument<200> doc;
    doc["lat"] = lat;
    doc["lon"] = lon;
    doc["elevation"] = elev;
    doc["speed"] = speed;
    doc["temp"] = temp;

    String jsonLine;
    serializeJson(doc, jsonLine);
    file.println(jsonLine);
    file.close();

    Serial.println("[SD] Saved: " + jsonLine);
    return true;
}
