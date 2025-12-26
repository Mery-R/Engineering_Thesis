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
{
    // Save instance for static callback
    _instance = this;
}

ThingsBoardClient* ThingsBoardClient::_instance = nullptr;

void ThingsBoardClient::onMqttMessage(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->processMessage(topic, payload, length);
    }
}


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
            
            _mqttClient.setCallback(onMqttMessage);
            _mqttClient.subscribe("v1/devices/me/rpc/request/+");
            _mqttClient.subscribe("v1/devices/me/attributes");
            _mqttClient.subscribe("v1/devices/me/attributes/response/+");
            
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

void ThingsBoardClient::setAttributesCallback(void (*callback)(const JsonObject &data)) {
    _attributesCallback = callback;
}

void ThingsBoardClient::requestSharedAttributes() {
    if (!_mqttClient.connected()) return;
    // Request specific shared keys
    const char* payload = "{\"sharedKeys\":\"BATCH_SIZE,Delay_MAIN,Delay_WIFI,MIN_BATCH_SIZE,REQUIRE_VALID_TIME,BUFFER_CAPACITY\"}";
    _mqttClient.publish("v1/devices/me/attributes/request/1", payload);
    Serial.println("[TB] Requested shared attributes");
}

void ThingsBoardClient::processMessage(char* topic, byte* payload, unsigned int length) {
    // Basic check to avoid buffer overflow
    if (length > 4096) return;
    
    // Create a temporary buffer and copy payload
    // (ArduinoJson needs a writable buffer or a String/const char*)
    // We can just parse it directly if we cast nicely or use a transient buffer
    // But payload is byte*, treating as string requires null termination if not careful
    // Safest is to use JsonDocument with the pointer and length
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
        Serial.print("[TB] deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    String topicStr = String(topic);

    // 1. Attributes (Push or Response)
    // Push: v1/devices/me/attributes
    // Response: v1/devices/me/attributes/response/+
    if (topicStr.startsWith("v1/devices/me/attributes")) {
        JsonObject data;
        if (doc.containsKey("shared")) {
            data = doc["shared"];
        } else {
            data = doc.as<JsonObject>();
        }

        if (_attributesCallback) {
            _attributesCallback(data);
        }
    }

    // 2. RPC
    // Topic: v1/devices/me/rpc/request/{id}
    else if (topicStr.startsWith("v1/devices/me/rpc/request/")) {
        const char* method = doc["method"];
        // Check for 'force' method or similar logic
        // User logic seemed to be just a generic "force" toggle, 
        // maybe looking for method "forceSend" or params "forced": true ?
        // Going by previous implementation hint: "rpcForceCallback(bool forced)"
        
        bool forced = false;
        if (doc.containsKey("params")) {
             if (doc["params"].is<bool>()) forced = doc["params"];
             else if (doc["params"].is<JsonObject>() && doc["params"].containsKey("forced")) forced = doc["params"]["forced"];
        }
        // Also check method name if needed
        
        if (_rpcCallback) {
            _rpcCallback(forced);
        }
    }
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
            //Serial.printf("[TB] Sent %d records directly from buffer\n", batch.size());
            return batch.size();
        } else {
            Serial.println("[TB][ERR] Publish failed");
            return 0;
        }
    } catch (...) {
        Serial.println("[TB][ERR] Exception during sendBatchDirect");
        return 0;
    }
}