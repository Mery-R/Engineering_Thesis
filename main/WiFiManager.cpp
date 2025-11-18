// WiFiManager.cpp
#include "WiFiManager.h"

WiFiManager::WiFiManager(const std::vector<WiFiConfig>& configs)
    : _configs(configs)
{
}

void WiFiManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    Serial.println("[WiFiManager] Initialized");
}

void WiFiManager::startScan() {
    Serial.println("[WiFiManager] Scanning networks...");
    int n = WiFi.scanNetworks(false, true);
    _availableSSIDs.clear();
    for (int i = 0; i < n; ++i) {
        _availableSSIDs.push_back(WiFi.SSID(i));
        if (debug) {
        Serial.printf("[WiFiManager] Found: %s (%d dBm)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
    }
    if(n == 0) Serial.println("[WiFiManager] No networks found");
}

const WiFiConfig* WiFiManager::chooseBestAP() {
    const WiFiConfig* best = nullptr;
    int32_t bestRSSI = -1000;
    for (auto& cfg : _configs) {
        for (auto& ssid : _availableSSIDs) {
            if (ssid == cfg.ssid) {
                int rssi = WiFi.RSSI(); // RSSI dla ostatniego skanu, można doprecyzować
                if (rssi > bestRSSI) {
                    bestRSSI = rssi;
                    best = &cfg;
                }
            }
        }
    }
    return best;
}

bool WiFiManager::connectToBest() {
    startScan();
    const WiFiConfig* cfg = chooseBestAP();
    if(!cfg) {
        Serial.println("[WiFiManager] No configured APs available");
        return false;
    }

    Serial.printf("[WiFiManager] Connecting to %s ...\n", cfg->ssid);
    WiFi.begin(cfg->ssid, cfg->password);

    unsigned long start = millis();
    while (millis() - start < 10000) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFiManager] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(200);
    }
    Serial.println("[WiFiManager] Connection failed");
    return false;
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    Serial.println("[WiFiManager] Disconnected");
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}
