// WiFiManager.h
#pragma once
#include <WiFi.h>
#include <vector>
#include <Arduino.h>

struct WiFiConfig {
    const char* ssid;
    const char* password;
};

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
    std::vector<String> _availableSSIDs;
    int debug = 0;
    const WiFiConfig* chooseBestAP(); // Wybór najlepszego AP po skanie
};
