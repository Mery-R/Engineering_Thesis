#pragma once
#include <WiFi.h>
#include <vector>
#include <Arduino.h>

struct WiFiConfig {
    const char* ssid;
    const char* password;
};

struct ScannedNetwork {
    String ssid;
    int rssi;
};

void WiFiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);

class WiFiManager {
public:
    WiFiManager(const std::vector<WiFiConfig>& configs);
    void begin();
    void startScan();
    bool connectToBest();
    void connectTo(const WiFiConfig* cfg);
    void disconnect();
    bool isConnected() const;

private:
    const WiFiConfig* chooseBestAP();

    std::vector<WiFiConfig> _configs;
    std::vector<ScannedNetwork> _scanned;
    int debug = 0;
};
