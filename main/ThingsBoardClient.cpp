#include "ThingsBoardClient.h"
#include <ArduinoJson.h>
#include <time.h>

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
{}


bool ThingsBoardClient::connect() {
    _mqttClient.setServer(_server, _port);
    _mqttClient.setBufferSize(4096);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[TB] WiFi not connected!");
        return false;
    }

    if (!_mqttClient.connected()) {
        Serial.printf("[TB] Connecting to MQTT broker %s:%d...\n", _server, _port);
        if (_mqttClient.connect(_clientId, _username, _password)) {
            Serial.println("[TB] Connected via MQTT!");
            Serial.printf("[TB] MQTT Buffer Size: %d\n", _mqttClient.getBufferSize());
             _mqttClient.subscribe("v1/devices/me/rpc/request/+");
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

void ThingsBoardClient::setRpcCallback(void (*callback)(bool forced)) {
    _rpcCallback = callback;
}

// Helper function to format telemetry data
static void formatTelemetry(JsonArray &batch) {
    for (int i = 0; i < batch.size(); ++i) {
        if (!batch[i].containsKey("ts")) {
            uint64_t ts_ms = 0ULL;
            if (batch[i].containsKey("timestamp")) {
                uint64_t raw = (uint64_t) batch[i]["timestamp"];
                if (raw >= 1000000000000ULL) ts_ms = raw;
                else if (raw >= 1000000000ULL) ts_ms = raw * 1000ULL;
            }
            if (ts_ms == 0ULL) {
                time_t now_s = time(NULL);
                if (now_s > 0) ts_ms = (uint64_t)now_s * 1000ULL + (uint64_t)(millis() % 1000);
                else ts_ms = (uint64_t) millis();
            }
            batch[i]["ts"] = ts_ms;
        }

        if (!batch[i].containsKey("values")) {
            JsonObject vals = batch[i].createNestedObject("values");
            const char* keys[] = {"lat","lon","elevation","speed","temp","time_source","last_gps_fix_timestamp","last_temp_read_timestamp","error_code"};
            for (const char* k : keys) {
                if (batch[i].containsKey(k)) vals[k] = batch[i][k];
            }
        }
    }
}

// -------------------
// Wysyła dane bezpośrednio z JsonArray (z ringBuffer) do ThingsBoard
// Format danych (JSON):
// [
//   {
//     "lat": double,
//     "lon": double,
//     "elevation": double,
//     "speed": double,
//     "temp": float,
//     "timestamp": uint64 (ms),
//     "time_source": int,
//     "last_gps_fix_timestamp": uint64 (ms),
//     "last_temp_read_timestamp": uint64 (ms),
//     "error_code": uint8
//   },
//   ...
// ]
int ThingsBoardClient::sendBatchDirect(JsonArray &batch) {
    if (batch.size() == 0) return 0;

    formatTelemetry(batch);

    try {
        String payload;
        serializeJson(batch, payload);

        if (!_mqttClient.connected() && !connect()) {
            Serial.println("[TB][ERR] MQTT not connected");
            return 0;
        }

        if (_mqttClient.publish("v1/devices/me/telemetry", payload.c_str())) {
            Serial.printf("[TB] Sent %d records directly from buffer\n", batch.size());
            return batch.size();
        } else {
            Serial.println("[TB][ERR] Direct publish failed");
            return 0;
        }
    } catch (...) {
        Serial.println("[TB][ERR] Exception during sendBatchDirect");
        return 0;
    }
}

// -------------------
// Wysyła rekordy z SD które mają tb_sent=false
int ThingsBoardClient::sendUnsent(SdModule &sdModule, int maxItems) {
    // Use JsonDocument for automatic memory management
    JsonDocument doc;
    JsonArray batch = doc.to<JsonArray>();

    int count = sdModule.readBatch(batch, maxItems, true); // true = only unsent
    if (count == 0) return 0;

    formatTelemetry(batch);

    try {
        String payload;
        serializeJson(batch, payload);
        Serial.printf("[TB][DBG] sendBatch bytes=%d items=%d\n", (int)payload.length(), batch.size());
        Serial.println(payload); // albo limituj: payload.substring(0,512)

        if (!_mqttClient.connected() && !connect()) {
            Serial.println("[TB][ERR] MQTT not connected");
            return 0;
        }

        if (_mqttClient.publish("v1/devices/me/telemetry", payload.c_str())) {
            Serial.printf("[TB] Sent %d unsent records from SD\n", count);
            sdModule.markBatchAsSent(batch);
            return count;
        } else {
            Serial.println("[TB][ERR] Unsent publish failed");
            return 0;
        }
    } catch (...) {
        Serial.println("[TB][ERR] Exception during sendUnsent");
        return 0;
    }
}