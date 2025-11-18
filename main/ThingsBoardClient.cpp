#include "ThingsBoardClient.h"
#include <ArduinoJson.h>

ThingsBoardClient::ThingsBoardClient(const char* server, int port,
                                     const char* clientId,
                                     const char* username,
                                     const char* password)
    : _server(server),
      _port(port),
      _clientId(clientId),
      _username(username),
      _password(password),
      _mqttClient(_wifiClient)
{
    _mqttClient.setBufferSize(16384); 
}


bool ThingsBoardClient::connect() {
    _mqttClient.setServer(_server, _port);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[TB] WiFi not connected!");
        return false;
    }

    if (!_mqttClient.connected()) {
        Serial.printf("[TB] Connecting to MQTT broker %s:%d...\n", _server, _port);
        if (_mqttClient.connect(_clientId, _username, _password)) {
            Serial.println("[TB] Connected via MQTT!");
            return true;
        } else {
            Serial.printf("[TB] MQTT connection failed. State: %d\n", _mqttClient.state());
            return false;
        }
    }

    return true;
}

bool ThingsBoardClient::isConnected() {
    return _mqttClient.connected();
}

void ThingsBoardClient::loop() {
    _mqttClient.loop();
}

// -------------------
// Hurtowa wysyłka batcha danych SD
int ThingsBoardClient::sendBatchToTB(SdModule &sdModule, int maxItems) {
    StaticJsonDocument<16384> doc;
    JsonArray batch = doc.to<JsonArray>();

    int count = sdModule.readBatch(batch, maxItems, false);
    if (count == 0) return 0;

    // Tworzymy JSON w formacie ThingsBoard: {ts: ..., values: {...}}
    StaticJsonDocument<16384> tbDoc;
    JsonArray tbBatch = tbDoc.to<JsonArray>();

    for (int i = 0; i < count; ++i) {
        JsonObject item = tbBatch.createNestedObject();
        item["ts"] = batch[i]["timestamp"];  // timestamp z SensorData
        JsonObject values = item.createNestedObject("values");

        values["lat"] = batch[i]["lat"];
        values["lon"] = batch[i]["lon"];
        values["elevation"] = batch[i]["elevation"];
        values["speed"] = batch[i]["speed"];
        values["temp"] = batch[i]["temp"];
        values["timestamp_time_source"] = batch[i]["timestamp_time_source"];
        values["last_gps_fix_timestamp"] = batch[i]["last_gps_fix_timestamp"];
        values["last_temp_read_timestamp"] = batch[i]["last_temp_read_timestamp"];
        values["error_code"] = batch[i]["error_code"];
        values["tb_sent"] = batch[i]["tb_sent"];
    }

    size_t jsonSize = measureJson(tbBatch) + 1;
    char* payload = (char*)malloc(jsonSize);
    if (!payload) {
        Serial.println("[TB][ERR] malloc failed!");
        return 0;
    }

    serializeJson(tbBatch, payload, jsonSize);

    if (!_mqttClient.connected() && !connect()) {
        Serial.println("[TB][ERR] MQTT not connected");
        free(payload);
        return 0;
    }

    if (_mqttClient.publish("v1/devices/me/telemetry", payload)) {
        Serial.printf("[TB] Sent %d records in batch\n", count);
        sdModule.markBatchAsSent(batch);
        free(payload);
        return count;
    } else {
        Serial.println("[TB][ERR] Publish failed");
        free(payload);
        return 0;
    }
}

