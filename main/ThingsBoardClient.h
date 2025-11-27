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

    // Register RPC handler for 'forced' boolean parameter
    void setRpcCallback(void (*callback)(bool forced));

private:
    const char* _server;
    int _port;
    const char* _clientId;
    const char* _username;
    const char* _password;

    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    
    void (*_rpcCallback)(bool forced) = nullptr;
    void handleRpc(const char* methodName, const JsonObject &params);
};