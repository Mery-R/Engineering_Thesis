#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include "SdModule.h"
#include <ArduinoJson.h>

// ThingsBoard client (MQTT)
class ThingsBoardClient {
public:
    ThingsBoardClient(const char* server, int port,
                      const char* clientId,
                      const char* username,
                      const char* password);

    bool connect(); // Connects to ThingsBoard
    bool isConnected(); // Checks if MQTT connection is alive
    void loop(); // Keeps MQTT connection alive
    void requestSharedAttributes(); // Request shared attributes from ThingsBoard
    void setAttributesCallback(void (*callback)(const JsonObject &data)); // Register callback for attributes updates
    int  sendBatchDirect(JsonArray &batch); // Sends data directly from JsonArray (from ringBuffer) to ThingsBoard
    void sendClientAttribute(const char* key, const char* value); // Sends a client attribute (static info)

private:
    const char* _server;
    int _port;
    const char* _clientId;
    const char* _username;
    const char* _password;

    WiFiClient _wifiClient;
    PubSubClient _mqttClient;

    static ThingsBoardClient* _instance; // Instance for static callback
    
    void (*_attributesCallback)(const JsonObject &data) = nullptr; // Attributes callback
    static void onMqttMessage(char* topic, byte* payload, unsigned int length); // Internal MQTT callback
    void processMessage(char* topic, byte* payload, unsigned int length); // Process MQTT message
    static const char* getMqttStateDescription(int state); // Get MQTT state description
};