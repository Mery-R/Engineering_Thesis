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

// Event handler for WiFi events
void WiFiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);

// WiFi Manager
class WiFiManager {
public:
    WiFiManager(const std::vector<WiFiConfig>& configs);

    void begin(); // Initializes WiFi
    void startScan(); // Starts network scan
    
    bool connectToBest(); // Connects to the best available network
    void connectTo(const WiFiConfig* cfg); // Connects to a specific network
    
    void disconnect(); // Disconnects from WiFi
    bool isConnected() const; // Checks if connected

private:
    std::vector<WiFiConfig> _configs;
    std::vector<ScannedNetwork> _scanned;
    int _debug = 0; // Debug flag (renamed from 'debug' to match style)

    const WiFiConfig* chooseBestAP(); // Selects the best AP from scanned list
};
