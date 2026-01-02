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

// -----------------------------------------------------
// --------------- Public Methods ----------------------
// -----------------------------------------------------

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
            _mqttClient.subscribe("v1/devices/me/attributes");
            _mqttClient.subscribe("v1/devices/me/attributes/response/+");
            
            return true;
        } else {
            int state = _mqttClient.state();
            Serial.printf("[TB] MQTT connection failed. State: %d (%s)\n", state, getMqttStateDescription(state));
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

void ThingsBoardClient::requestSharedAttributes() {
    if (!_mqttClient.connected()) return;
    // Request specific shared keys
    const char* payload = "{\"sharedKeys\":\"SEND_BATCH_SIZE,Delay_MAIN,Delay_WIFI,BUFFER_SEND_THRESHOLD,REQUIRE_VALID_TIME,BUFFER_CAPACITY\"}";
    _mqttClient.publish("v1/devices/me/attributes/request/1", payload);
    Serial.println("[TB] Requested shared attributes");
}

void ThingsBoardClient::setAttributesCallback(void (*callback)(const JsonObject &data)) {
    _attributesCallback = callback;
}

int ThingsBoardClient::sendBatchDirect(JsonArray &batch) {
    if (batch.size() == 0) return 0;

    String payload;
    // Serialization to String (required by PubSubClient)
    if (serializeJson(batch, payload) == 0) {
        Serial.println("[TB][ERR] Serialization failed (Out of memory?)");
        return 0;
    }

    // Automatic reconnection if lost
    if (!_mqttClient.connected()) {
        if (!connect()) {
            // Serial.println("[TB][ERR] Cannot send - MQTT disconnected");
            return 0;
        }
    }

    // Increase MQTT buffer if payload is large
    if (payload.length() > _mqttClient.getBufferSize()) {
        _mqttClient.setBufferSize(payload.length() + 100);
    }

    // Publish to telemetry topic
    if (_mqttClient.publish("v1/devices/me/telemetry", payload.c_str())) {
        return batch.size();
    } else {
        Serial.println("[TB][ERR] Publish failed!");
        return 0;
    }
}

void ThingsBoardClient::sendClientAttribute(const char* key, const char* value) {
    if (!_mqttClient.connected()) return;

    String topic = "v1/devices/me/attributes";
    
    // Create JSON payload: {"key": "value"}
    JsonDocument doc;
    doc[key] = value;
    
    String payload;
    serializeJson(doc, payload);

    if (_mqttClient.publish(topic.c_str(), payload.c_str())) {
        Serial.printf("[TB] Sent client attribute: %s = %s\n", key, value);
    } else {
        Serial.printf("[TB] Failed to send attribute: %s\n", key);
    }
}

// -----------------------------------------------------
// --------------- Private Methods ---------------------
// -----------------------------------------------------

ThingsBoardClient* ThingsBoardClient::_instance = nullptr;

void ThingsBoardClient::onMqttMessage(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->processMessage(topic, payload, length);
    }
}

void ThingsBoardClient::processMessage(char* topic, byte* payload, unsigned int length) {
    // Basic check to avoid buffer overflow
    if (length > 4096) return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
        Serial.print("[TB] deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    String topicStr = String(topic);

    // Attributes (Push or Response)
    // Push: v1/devices/me/attributes
    // Response: v1/devices/me/attributes/response/+
    if (topicStr.startsWith("v1/devices/me/attributes")) {
        Serial.printf("[TB] Attributes received on: %s\n", topicStr.c_str());
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
}

const char* ThingsBoardClient::getMqttStateDescription(int state) {
    switch (state) {
        case -4: return "MQTT_CONNECTION_TIMEOUT";
        case -3: return "MQTT_CONNECTION_LOST";
        case -2: return "MQTT_CONNECT_FAILED";
        case -1: return "MQTT_DISCONNECTED";
        case 0:  return "MQTT_CONNECTED";
        case 1:  return "MQTT_CONNECT_BAD_PROTOCOL";
        case 2:  return "MQTT_CONNECT_BAD_CLIENT_ID";
        case 3:  return "MQTT_CONNECT_UNAVAILABLE";
        case 4:  return "MQTT_CONNECT_BAD_CREDENTIALS";
        case 5:  return "MQTT_CONNECT_UNAUTHORIZED";
        default: return "UNKNOWN";
    }
}
