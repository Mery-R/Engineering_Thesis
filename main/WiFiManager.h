// WiFiManager.h
#pragma once
#include <WiFi.h>
#include <vector>
#include <Arduino.h>

struct WiFiConfig {
    const char* ssid;
    const char* password;
};

struct ScannedNetwork { String ssid; int rssi; };

class WiFiManager {
public:
    WiFiManager(const std::vector<WiFiConfig>& configs);

    void begin();                    // Inicjalizacja WiFi
    void startScan();                 // Skanowanie sieci
    bool connectToBest();             // Łączy z najlepszą dostępną siecią z listy
    void disconnect();
    bool isConnected() const;

private:
    std::vector<WiFiConfig> _configs;
    std::vector<ScannedNetwork> _scanned;
    int debug = 0;
    const WiFiConfig* chooseBestAP(); // Wybór najlepszego AP po skanie
};
