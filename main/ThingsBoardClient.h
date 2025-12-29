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

    bool connect();
    bool isConnected();
    void loop();

    // Wysyła dane bezpośrednio z JsonArray (z ringBuffer) do ThingsBoard
    int sendBatchDirect(JsonArray &batch);

    // Wysyła rekordy z SD które mają tb_sent=false
    int sendUnsent(SdModule &sdModule, int maxItems);

    // Register RPC handler for 'forced' boolean parameter (Removed)
    // void setRpcCallback(void (*callback)(bool forced));

    // Request shared attributes from ThingsBoard
    void requestSharedAttributes();

    // Register callback for attributes updates
    void setAttributesCallback(void (*callback)(const JsonObject &data));

private:
    const char* _server;
    int _port;
    const char* _clientId;
    const char* _username;
    const char* _password;

    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    
    // void (*_rpcCallback)(bool forced) = nullptr;
    void (*_attributesCallback)(const JsonObject &data) = nullptr;
    
    // Internal MQTT callback
    static void onMqttMessage(char* topic, byte* payload, unsigned int length);
    void processMessage(char* topic, byte* payload, unsigned int length);

    // void handleRpc(const char* methodName, const JsonObject &params);
    
    static ThingsBoardClient* _instance;
};